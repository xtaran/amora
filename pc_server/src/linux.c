/**
 * @file   linux.c
 * @author Adenilson Cavalcanti
 * @date   Tue May 15 22:54:03 2007
 *
 * @brief  Linux bluetooth code.
 *
 * BlueZ dependent code goes here.
 */

/*  Copyright (C) 2007  Adenilson Cavalcanti <savagobr@yahoo.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; by version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "bluecode.h"
#include "protocol.h"
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

struct service_description *build_sd(int channel)
{
	struct service_description *sd;
	sd = (struct service_description *)
		malloc(sizeof(struct service_description));

	if (!sd)
		goto mem_error;

	sd->channel = channel;
	sd->uuid = 0xAFFF;
	sd->service_name = strdup("Amora: assistant ");
	sd->service_dsc = strdup("A X handler for cellphone ");
	sd->service_prov = strdup("Savago ");

	if (sd->service_name && sd->service_dsc && sd->service_prov)
		goto exit;

mem_error:
	printf("build_sd: failed service allocation structure!");
	if (sd)
		free(sd);
	sd = NULL;

exit:

	return sd;
}


void destroy_sd(struct service_description *sd)
{

	free(sd->service_name);
	free(sd->service_dsc);
	free(sd->service_prov);
	sdp_close((sdp_session_t *)sd->session);
}

int read_socket(int client, char *data, int length)
{
	int res = 0, tmp;
	memset(data, 0, length);
	--length;

	while (!strchr(data, CMD_BREAK) && (res < length)) {
		/* TODO: add error checking here. */
		tmp = read(client, data, length);
		res += tmp;
		length -= res;
	}
	/* Adding ending of string */
	data[res] = '\0';
	return res;
}


int build_bluetooth_socket(unsigned int channel)
{
	struct sockaddr_rc loc_addr;
	int s, res = -1;

	memset(&loc_addr, 0, sizeof(struct sockaddr_rc));

	s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if (s == -1)
		goto error;

	/* FIXME: use SDP in future */
	loc_addr.rc_family = AF_BLUETOOTH;
	/* TODO: how to test for available bluetooth dongles? */
	loc_addr.rc_bdaddr = *BDADDR_ANY;

	loc_addr.rc_channel = (uint8_t) channel;
	res = bind(s, (struct sockaddr *)&loc_addr, sizeof(loc_addr));
	if (res == -1)
		goto close;

	goto exit;

close:
	fprintf(stderr, "closing socket...\n");
	close(s);
	s = -1;

error:
	fprintf(stderr, "build_bluetooth_socket error!\n");

exit:
	return s;
}


int describe_service(struct service_description *sd)
{


	uint32_t service_uuid_int[] = { 0, 0, 0, sd->uuid };
	uint8_t rfcomm_channel = sd->channel;
	const char *service_name = sd->service_name;
	const char *service_dsc = sd->service_dsc;
	const char *service_prov = sd->service_prov;

	uuid_t root_uuid, l2cap_uuid, rfcomm_uuid, svc_uuid;
	sdp_list_t *l2cap_list = 0, *rfcomm_list = 0, *root_list = 0,
		*proto_list = 0, *access_proto_list = 0,  *svclass_id = 0,
		*profiles = 0;

	sdp_data_t *channel = 0;
	sdp_record_t *record = sdp_record_alloc();

	int err = -1;
	sdp_session_t *session = 0;
	sdp_profile_desc_t profile;

	// set the general service ID
	sdp_uuid128_create(&svc_uuid, &service_uuid_int);
	sdp_set_service_id(record, svc_uuid);

	// make the service record publicly browsable
	sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
	root_list = sdp_list_append(0, &root_uuid);
	sdp_set_browse_groups(record, root_list);

	//Set port attributes
	sdp_uuid16_create(&root_uuid, SERIAL_PORT_SVCLASS_ID);
	svclass_id = sdp_list_append(0, &root_uuid);
	sdp_set_service_classes(record, svclass_id);

	sdp_uuid16_create(&profile.uuid, SERIAL_PORT_PROFILE_ID);
	profile.version = 0x0100;
	profiles = sdp_list_append(0, &profile);
	sdp_set_profile_descs(record, profiles);


	// set l2cap information
	sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
	l2cap_list = sdp_list_append(0, &l2cap_uuid);
	proto_list = sdp_list_append(0, l2cap_list);

	// set rfcomm information
	sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
	channel = sdp_data_alloc(SDP_UINT8, &rfcomm_channel);
	rfcomm_list = sdp_list_append(0, &rfcomm_uuid);
	sdp_list_append(rfcomm_list, channel);
	sdp_list_append(proto_list, rfcomm_list);

	// attach protocol information to service record
	access_proto_list = sdp_list_append(0, proto_list);
	sdp_set_access_protos(record, access_proto_list);

	// set the name, provider, and description
	sdp_set_info_attr(record, service_name, service_prov, service_dsc);

	// connect to the local SDP server, register the service record, and
	// disconnect
	session = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY);
	if (!session) {
		printf("describe_service: cannot connect to bluetooth"
		       " device. Is bluetooth daemon running?\n");
		goto exit;
	}

	err = sdp_record_register(session, record, 0);
	if (err == -1) {
		printf("describe_service: error registering service!");
	}
exit:
	// cleanup
	sdp_data_free(channel);
	sdp_list_free(l2cap_list, 0);
	sdp_list_free(rfcomm_list, 0);
	sdp_list_free(root_list, 0);
	sdp_list_free(access_proto_list, 0);
	sdp_list_free(svclass_id, 0);
	sdp_list_free(profiles, 0);
	sd->session = (void*) session;

	return err;
}

