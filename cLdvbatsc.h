/*
 * cLdvbatsc.h
 * Gokhan Poyraz <gokhan@kylone.com>
 * Based on code from:
 *****************************************************************************
 * atsc_psip_section.h
 * https://bitbucket.org/CrazyCat/scan-s2/
 *****************************************************************************
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef CLDVBATSC_H_
#define CLDVBATSC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRC_LEN 4
#define PACKED __attribute((packed))

#define ATSC_EXTENDED_CHANNEL_NAME_DESCRIPTOR_ID 0xA0
#define ATSC_SERVICE_LOCATION_DESCRIPTOR_ID 0xA1

   struct ATSC_extended_channel_name_descriptor {
         uint8_t  descriptor_tag            : 8;
         uint8_t  descriptor_length         : 8;
         uint8_t  TODO                      : 1;
   } PACKED;

   struct ATSC_service_location_descriptor {
         uint8_t  descriptor_tag            : 8;
         uint8_t  descriptor_length         : 8;
         uint8_t  reserved                  : 3;
         uint16_t PCR_PID                   :13;
         uint8_t  number_elements           : 8;
   } PACKED;

   struct ATSC_service_location_element {
         uint8_t  stream_type               : 8;
         uint8_t  reserved                  : 3;
         uint16_t elementary_PID            :13;
         uint32_t ISO_639_language_code     :24;
   } PACKED;

   struct tvct_channel {
         uint16_t short_name0               :16;
         uint16_t short_name1               :16;
         uint16_t short_name2               :16;
         uint16_t short_name3               :16;
         uint16_t short_name4               :16;
         uint16_t short_name5               :16;
         uint16_t short_name6               :16;
         uint8_t  reserved0                 : 4;
         uint16_t major_channel_number      :10;
         uint16_t minor_channel_number      :10;
         uint8_t  modulation_mode           : 8;
         uint32_t carrier_frequency         :32;
         uint16_t channel_TSID              :16;
         uint16_t program_number            :16;
         uint8_t  ETM_location              : 2;
         uint8_t  access_controlled         : 1;
         uint8_t  hidden                    : 1;
         uint8_t  reserved1                 : 2;
         uint8_t  hide_guide                : 1;
         uint8_t  reserved2                 : 3;
         uint8_t  service_type              : 6;
         uint16_t source_id                 :16;
         uint8_t  reserved3                 : 6;
         uint16_t descriptors_length        :10;
   } PACKED;

   extern uint32_t getBits (const uint8_t *buf, int startbit, int bitlen);
   extern struct ATSC_extended_channel_name_descriptor read_ATSC_extended_channel_name_descriptor(const uint8_t *);
   extern struct ATSC_service_location_descriptor read_ATSC_service_location_descriptor(const uint8_t *);
   extern struct ATSC_service_location_element read_ATSC_service_location_element(const uint8_t *);
   extern struct tvct_channel read_tvct_channel(const uint8_t *);

#ifdef __cplusplus
}
#endif

#endif /*CLDVBATSC_H_*/
