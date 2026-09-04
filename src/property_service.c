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
bool get_version_cb(LSHandle *handle, LSMessage *message, void *user_data);

static LSMethod property_service_methods[]  = {
	{ "setProperty", set_property_cb },
	{ "getProperty", get_property_cb },
	{ "getAllProperties", get_all_properties_cb },
	{ "getVersion", get_version_cb },
	{ NULL, NULL }
};

/* The vendor part of an Android system is not required to match the version of
 * the platform it is running with, so the VNDK version is looked up separately
 * from the Android version itself. Both are reported through different property
 * names depending on the Android version the vendor image was built for, so try
 * all known ones in order of preference. */
static const char * const android_version_keys[] = {
	"ro.build.version.release",
	"ro.system.build.version.release",
	NULL,
};

static const char * const android_sdk_version_keys[] = {
	"ro.build.version.sdk",
	"ro.system.build.version.sdk",
	NULL,
};

static const char * const vndk_version_keys[] = {
	"ro.vndk.version",
	"ro.vendor.vndk.version",
	"ro.product.vndk.version",
	"ro.board.api_level",
	"ro.board.first_api_level",
	"ro.vendor.api_level",
	NULL,
};

/* Stores the value of the first key which is set to a non empty value in value,
 * which has to be at least PROP_VALUE_MAX bytes long. */
static void lookup_first_property(const char * const *keys, char *value)
{
	int n;

	for (n = 0; keys[n] != NULL; n++) {
		property_get(keys[n], value, "");

		if (strlen(value) > 0)
			return;
	}
}

bool set_property_cb(LSHandle *handle, LSMessage *message, void *user_data)
{
	struct property_service *service = user_data;

	return true;
}

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

	reply_obj = jobject_create();
	props_obj = jarray_create(NULL);

	if (property_list(record_prop, props_obj) < 0) {
		luna_service_message_reply_error_internal(handle, message);
		goto cleanup;
	}

	jobject_put(reply_obj, J_CSTR_TO_JVAL("properties"), props_obj);
	jobject_put(reply_obj, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

	luna_service_message_validate_and_send(handle, message, reply_obj);

cleanup:
	if (!jis_null(reply_obj))
		j_release(&reply_obj);

	return true;
}

bool get_property_cb(LSHandle *handle, LSMessage *message, void *user_data)
{
	jvalue_ref parsed_obj = NULL;
	jvalue_ref keys_obj = NULL;
	jvalue_ref reply_obj = NULL;
	jvalue_ref props_obj = NULL;
	jvalue_ref prop_obj = NULL;
	const char *payload;
	char value[PROP_VALUE_MAX];
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
		luna_service_message_reply_error_invalid_params(handle, message);
		goto cleanup;
	}

	reply_obj = jobject_create();
	props_obj = jarray_create(NULL);

	for (n = 0; n < jarray_size(keys_obj); n++) {
		jvalue_ref key_obj = jarray_get(keys_obj, n);

		if (!jis_string(key_obj))
			continue;

		/* jstring_get returns a copy of the string which we own and have to
		 * release again once we're done with it. */
		key_buf = jstring_get(key_obj);

		if (strlen(key_buf.m_str) > 0) {
			property_get(key_buf.m_str, value, "");

			prop_obj = jobject_create();
			jobject_put(prop_obj, jstring_create(key_buf.m_str), jstring_create(value));

			jarray_append(props_obj, prop_obj);
		}

		jstring_free_buffer(key_buf);
	}

	jobject_put(reply_obj, J_CSTR_TO_JVAL("properties"), props_obj);
	jobject_put(reply_obj, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

	luna_service_message_validate_and_send(handle, message, reply_obj);

cleanup:
	if (!jis_null(parsed_obj))
		j_release(&parsed_obj);

	if (!jis_null(reply_obj))
		j_release(&reply_obj);

	return true;
}

bool get_version_cb(LSHandle *handle, LSMessage *message, void *user_data)
{
	jvalue_ref reply_obj = NULL;
	char value[PROP_VALUE_MAX];

	reply_obj = jobject_create();

	lookup_first_property(android_version_keys, value);
	jobject_put(reply_obj, J_CSTR_TO_JVAL("androidVersion"), jstring_create(value));

	lookup_first_property(android_sdk_version_keys, value);
	jobject_put(reply_obj, J_CSTR_TO_JVAL("androidSdkVersion"), jstring_create(value));

	lookup_first_property(vndk_version_keys, value);
	jobject_put(reply_obj, J_CSTR_TO_JVAL("vndkVersion"), jstring_create(value));

	jobject_put(reply_obj, J_CSTR_TO_JVAL("returnValue"), jboolean_create(true));

	luna_service_message_validate_and_send(handle, message, reply_obj);

	j_release(&reply_obj);

	return true;
}

struct property_service* property_service_create(void)
{
	struct property_service *service;
	LSError error;

	service = g_try_new0(struct property_service, 1);
	if (!service)
		return NULL;

	LSErrorInit(&error);

	if (!LSRegister("com.android.properties", &service->handle, &error)) {
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

	if (service->handle != NULL && !LSUnregister(service->handle, &error)) {
		g_error("Could not unregister service: %s", error.message);
		LSErrorFree(&error);
	}

	g_free(service);
}

// vim:ts=4:sw=4:noexpandtab
