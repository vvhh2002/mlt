/*
 * producer_ladspa.c -- LADSPA plugin producer
 * Copyright (C) 2013 Ushodaya Enterprises Limited
 * Author: Brian Matherly <pez4brian@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <framework/mlt_producer.h>
#include <framework/mlt_frame.h>
#include <framework/mlt_log.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <string.h>

#include "jack_rack.h"

#define BUFFER_LEN 10000

/** One-time initialization of jack rack.
*/

static jack_rack_t* initialise_jack_rack( mlt_properties properties, int channels )
{
	jack_rack_t *jackrack = NULL;
	unsigned long plugin_id = mlt_properties_get_int64( properties, "_pluginid" );

	// Start JackRack
	if ( plugin_id )
	{
		// Create JackRack without Jack client name so that it only uses LADSPA
		jackrack = jack_rack_new( NULL, channels );
		mlt_properties_set_data( properties, "_jackrack", jackrack, 0,
			(mlt_destructor) jack_rack_destroy, NULL );

		// Load one LADSPA plugin by its UniqueID
		plugin_desc_t *desc = plugin_mgr_get_any_desc( jackrack->plugin_mgr, plugin_id );
		plugin_t *plugin;

		if ( desc && ( plugin = jack_rack_instantiate_plugin( jackrack, desc ) ) )
		{
			LADSPA_Data value;
			int index, c;

			plugin->enabled = TRUE;
			plugin->wet_dry_enabled = FALSE;
			for ( index = 0; index < desc->control_port_count; index++ )
			{
				// Apply the control port values
				char key[20];
				value = plugin_desc_get_default_control_value( desc, index, sample_rate );
				snprintf( key, sizeof(key), "%d", index );
				if ( mlt_properties_get( properties, key ) )
					value = mlt_properties_get_double( properties, key );
				for ( c = 0; c < plugin->copies; c++ )
					plugin->holders[c].control_memory[index] = value;
			}
			process_add_plugin( jackrack->procinfo, plugin );
		}
		else
		{
			mlt_log_error( properties, "failed to load plugin %lu\n", plugin_id );
		}
	}

	return jackrack;
}

static int producer_get_audio( mlt_frame frame, void **buffer, mlt_audio_format *format, int *frequency, int *channels, int *samples )
{
	// Get the producer service
	mlt_producer producer = mlt_properties_get_data( MLT_FRAME_PROPERTIES( frame ), "_producer_ladspa", NULL );
	mlt_properties producer_properties = MLT_PRODUCER_PROPERTIES( producer );
	int size = 0;
	LADSPA_Data** output_buffers = NULL;
	int i = 0;

	// Initialize LADSPA if needed
	jack_rack_t *jackrack = mlt_properties_get_data( producer_properties, "_jackrack", NULL );
	if ( !jackrack )
	{
		sample_rate = *frequency; // global inside jack_rack
		jackrack = initialise_jack_rack( producer_properties, *channels );
	}

	if( jackrack )
	{
		// Correct the returns if necessary
		*samples = *samples <= 0 ? 1920 : *samples;
		*channels = *channels <= 0 ? 2 : *channels;
		*frequency = *frequency <= 0 ? 48000 : *frequency;
		*format = mlt_audio_float;

		// Calculate the size of the buffer
		size = *samples * *channels * sizeof( float );

		// Allocate the buffer
		*buffer = mlt_pool_alloc( size );

		// Initialize the LADSPA output buffer.
		output_buffers = mlt_pool_alloc( sizeof( LADSPA_Data* ) * *channels );
		for ( i = 0; i < *channels; i++ )
		{
			output_buffers[i] = (LADSPA_Data*) *buffer + i * *samples;
		}

		// Do LADSPA processing
		process_ladspa( jackrack->procinfo, *samples, NULL, output_buffers );
		mlt_pool_release( output_buffers );

		// Set the buffer for destruction
		mlt_frame_set_audio( frame, *buffer, *format, size, mlt_pool_release );
	}

	return 0;
}

static int producer_get_frame( mlt_producer producer, mlt_frame_ptr frame, int index )
{
	// Generate a frame
	*frame = mlt_frame_init( MLT_PRODUCER_SERVICE( producer ) );

	// Check that we created a frame and initialize it
	if ( *frame != NULL )
	{
		// Obtain properties of frame
		mlt_properties frame_properties = MLT_FRAME_PROPERTIES( *frame );

		// Update timecode on the frame we're creating
		mlt_frame_set_position( *frame, mlt_producer_position( producer ) );

		// Save the producer to be used in get_audio
		mlt_properties_set_data( frame_properties, "_producer_ladspa", producer, 0, NULL, NULL );

		// Push the get_audio method
		mlt_frame_push_audio( *frame, producer_get_audio );
	}

	// Calculate the next time code
	mlt_producer_prepare_next( producer );

	return 0;
}

/** Destructor for the producer.
*/

static void producer_close( mlt_producer producer )
{
	producer->close = NULL;
	mlt_producer_close( producer );
	free( producer );
}

/** Constructor for the producer.
*/

mlt_producer producer_ladspa_init( mlt_profile profile, mlt_service_type type, const char* id, char* arg )
{
	// Create a new producer object
	mlt_producer producer = mlt_producer_new( profile );

	if ( producer != NULL )
	{
		mlt_properties properties = MLT_PRODUCER_PROPERTIES( producer );

		producer->get_frame = producer_get_frame;
		producer->close = ( mlt_destructor )producer_close;

		// Save the plugin ID.
		if ( !strncmp( id, "ladspa.", 7 ) )
		{
			mlt_properties_set( properties, "_pluginid", id + 7 );
		}

		// Make sure the plugin ID is valid.
		unsigned long plugin_id = mlt_properties_get_int64( properties, "_pluginid" );
		if( plugin_id < 1000 || plugin_id > 0x00FFFFFF )
		{
			producer_close( producer );
			producer = NULL;
		}
	}
	return producer;
}
