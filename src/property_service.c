/* @@@LICENSE
*
* Copyright (c) 2013 Simon Busch <morphis@gravedo.de>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <glib.h>
#include <pbnjson.h>
#include <luna-service2/lunaservice.h>
#include <hybris/properties/properties.h>

#include "property_service.h"
#include "luna_service_utils.h"

extern GMainLoop *event_loop;

struct property_service {
	LSHandle *handle;
};

bool set_property_cb(LSHandle *handle, LSMessage *message, void *user_data);
bool get_property_cb(LSHandle *handle, LSMessage *message, void *user_data);
bool get_all_properties_cb(LSHandle *handle, LSMessage *message, void *user_data);

static LSMethod property_service_methods[]  = {
	{ "setProperty", set_property_cb },
	{ "getProperty", get_property_cb },
	{ "getAllProperties", get_all_properties_cb },
	{ NULL, NULL }
};

bool set_property_cb(LSHandle *handle, LSMessage *message, void *user_data)
{
	struct property_service *service = user_data;

	return true;
}

struct property_list {
	int count;
	char** items;
};

static void record_prop(const char *key, const char *value, void *user_data)
{
	jvalue_ref props_obj = user_data;
	jvalue_ref prop_obj = NULL;

	prop_obj = jobject_create();
	jobject_put(prop_obj, jstring_create(key), jstring_create(value));

	jarray_append(props_obj, prop_obj);
}

bool get_all_properties_cb(LSHandle *handle, LSMessage *message, void *user_data)
{
	jvalue_ref reply_obj = NULL;
	jvalue_ref props_obj = NULL;
	struct property_list list;

	reply_obj = jobject_create();
	props_obj = jarray_create(NULL);

	memset(&list, 0, sizeof(struct property_list));
	if (property_list(record_prop, props_obj) < 0) {
		luna_service_message_reply_error_internal(handle, message);
		goto cleanup;
	}

	jobject_put(reply_obj, J_CSTR_TO_JVAL("properties"), props_obj);
	jobject_put(reply_obj, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

	if (!luna_service_message_validate_and_send(handle, message, reply_obj))
		goto cleanup;

cleanup:
	if (!jis_null(reply_obj))
		j_release(&reply_obj);

	if (!jis_null(props_obj))
		j_release(&props_obj);

	return true;
}

bool get_property_cb(LSHandle *handle, LSMessage *message, void *user_data)
{
	jvalue_ref parsed_obj = NULL;
	jvalue_ref keys_obj = NULL;
	jvalue_ref reply_obj = NULL;
	jvalue_ref props_obj = NULL;
	jvalue_ref prop_obj = NULL;
	char *payload, value[PROP_VALUE_MAX];
	int n;
	raw_buffer key_buf;

	payload = LSMessageGetPayload(message);
	parsed_obj = luna_service_message_parse_and_validate(payload);
	if (jis_null(parsed_obj)) {
		luna_service_message_reply_error_bad_json(handle, message);
		goto cleanup;
	}

	if (!jobject_get_exists(parsed_obj, J_CSTR_TO_BUF("keys"), &keys_obj) ||
		!jis_array(keys_obj)) {
		luna_service_message_reply_error_bad_json(handle, message);
		goto cleanup;
	}

	reply_obj = jobject_create();
	props_obj = jarray_create(NULL);

	for (n = 0; n < jarray_size(keys_obj); n++) {
		jvalue_ref key_obj = jarray_get(keys_obj, n);

		if (!jis_string(key_obj))
			continue;

		key_buf = jstring_get(key_obj);

		if (strlen(key_buf.m_str) == 0)
			continue;

		property_get(key_buf.m_str, value, "");

		prop_obj = jobject_create();
		jobject_put(prop_obj, jstring_create(key_buf.m_str), jstring_create(value));

		jarray_append(props_obj, prop_obj);
	}

	jobject_put(reply_obj, J_CSTR_TO_JVAL("properties"), props_obj);
	jobject_put(reply_obj, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

	if (!luna_service_message_validate_and_send(handle, message, reply_obj))
		goto cleanup;

cleanup:
	if (!jis_null(parsed_obj))
		j_release(&parsed_obj);

	if (!jis_null(reply_obj))
		j_release(&reply_obj);

	return true;
}

struct property_service* property_service_create()
{
	struct property_service *service;
	LSError error;

	service = g_try_new0(struct property_service, 1);
	if (!service)
		return NULL;

	LSErrorInit(&error);

	if (!LSRegisterPubPriv("com.android.properties", &service->handle, false, &error)) {
		g_error("Failed to register the luna service: %s", error.message);
		LSErrorFree(&error);
		goto error;
	}

	if (!LSRegisterCategory(service->handle, "/", property_service_methods,
			NULL, NULL, &error)) {
		g_error("Could not register service category: %s", error.message);
		LSErrorFree(&error);
		goto error;
	}

	if (!LSCategorySetData(service->handle, "/", service, &error)) {
		g_error("Could not set daa for service category: %s", error.message);
		LSErrorFree(&error);
		goto error;
	}

	if (!LSGmainAttach(service->handle, event_loop, &error)) {
		g_error("Could not attach service handle to mainloop: %s", error.message);
		LSErrorFree(&error);
		goto error;
	}

	return service;

error:
	if (service->handle != NULL) {
		LSUnregister(service->handle, &error);
		LSErrorFree(&error);
	}

	g_free(service);

	return NULL;
}

void property_service_free(struct property_service *service)
{
	LSError error;

	LSErrorInit(&error);

	if (service->handle != NULL && LSUnregister(service->handle, &error) < 0) {
		g_error("Could not unregister service: %s", error.message);
		LSErrorFree(&error);
	}

	g_free(service);
}

// vim:ts=4:sw=4:noexpandtab
