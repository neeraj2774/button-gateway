/***************************************************************************************************
 * Copyright (c) 2016, Imagination Technologies Limited and/or its affiliated group companies
 * and/or licensors
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions
 *    and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file flow_interface.c
 * @brief Interface functions for initializing libflow, registering as a device using FlowCore SDK,
 *        and sending flow messages to user using FlowMessaging SDK once registration is successful.
 */

/***************************************************************************************************
 * Includes
 **************************************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <flow/flowmessaging.h>
#include <libconfig.h>
#include "log.h"

/***************************************************************************************************
 * Definitions
 **************************************************************************************************/

/** Max array size of registration data elements. */
#define MAX_SIZE (256)
/** Message expiry timeout on flow cloud. */
#define MESSAGE_EXPIRY_TIMEOUT (20)
/** Configuration file to get registration data stored by provisioning app. */
#define CONFIG_FILE "/etc/lwm2m/flow_access.cfg"
/** Number of trials for reading configuration file */
#define FILE_READ_TRIALS (5)

/***************************************************************************************************
 * Typedef
 **************************************************************************************************/

/**
 * A structure to contain flow registration data.
 */
typedef struct
{
	/*@{*/
	char url[MAX_SIZE]; /**< flow server url */
	char key[MAX_SIZE]; /**< customer authentification key */
	char secret[MAX_SIZE]; /**< customer secret key */
	char rememberMeToken[MAX_SIZE]; /**< remember me token */
	/*@}*/
}RegistrationData;


/***************************************************************************************************
 * Implementation
 **************************************************************************************************/

/**
 * @brief Get value for corresponding key from the configuration file.
 * @param *cfg pointer to configuration object.
 * @param *dest value string of the key.
 * @param *key key to be searched in configuration file.
 * @return true if configuration read successfuly, else false.
 */
static bool GetValueForKey(config_t *cfg, char *dest, const char *key)
{
	const char *tmp;

	if (config_lookup_string(cfg, key, &tmp) != CONFIG_FALSE)
	{
		strncpy(dest, tmp, MAX_SIZE - 1);
		dest[MAX_SIZE - 1] = '\0';
		return true;
	}
	return false;
}

/**
 * @brief Read device resgitration settings from configuration file.
 *        And if not found, ask user for settings.
 * @param *regData pointer to device registration data.
 * @return true if configuration read successfuly, else false.
 */
static bool GetConfigData(RegistrationData *regData)
{
	config_t cfg;
	bool success = false;
	int i;

	config_init(&cfg);

	for (i = FILE_READ_TRIALS; i > 0; i--)
	{
		if (!config_read_file(&cfg, CONFIG_FILE))
		{
			LOG(LOG_INFO, "Waiting for config data");
			sleep(1);
		}
		else
		{
			if (GetValueForKey(&cfg, regData->url, "URL") &&
				GetValueForKey(&cfg, regData->key, "CustomerKey") &&
				GetValueForKey(&cfg, regData->secret, "CustomerSecret") &&
				GetValueForKey(&cfg, regData->rememberMeToken, "RememberMeToken"))
			{
				success = true;
			}
			else
			{
				LOG(LOG_ERR, "Failed to read config data");
			}
			config_destroy(&cfg);
			break;
		}
	}

	if (i == 0)
	{
		LOG(LOG_ERR, "Failed to read config file");
		config_destroy(&cfg);
	}
	return success;
}

/**
 * @brief Initialize libflowcore and libflowmessaging.
 * @param *url pointer to server url.
 * @param *key pointer to customer authentication key.
 * @param *secret pointer to customer secret key.
 * @param *rememberMeToken remember me token for flow cloud access.
 * @return true if libflow initialization is successful, else false.
 */
static bool InitialiseLibFlow(const char *url,
								const char *key,
								const char *secret,
								const char *rememberMeToken)
{
	if (FlowCore_Initialise())
	{
		size_t length = strlen(rememberMeToken) +1;

		FlowNVS_Set("core.deviceremembermetoken", rememberMeToken, length);

		FlowCore_Shutdown();

		if (FlowCore_Initialise())
		{
			FlowCore_RegisterTypes();

			if (FlowMessaging_Initialise())
			{
				if (FlowClient_ConnectToServer(url, key, secret, true))
				{
					return true;
				}
				else
				{
					FlowCore_Shutdown();
					FlowMessaging_Shutdown();
					LOG(LOG_ERR, "Failed to connect to server");
				}
			}
			else
			{
				LOG(LOG_ERR, "Flow Messaging initialization failed");
			}
		}
		else
		{
			LOG(LOG_ERR, "Flow Core re-initialization failed");
		}
	}
	else
	{
		LOG(LOG_ERR, "Flow Core initialization failed");
	}
	return false;
}

/**
 * @brief Get user id to which the device is registered.
 * @param *userId pointer to device's user Id.
 * @return true if user id is retrieved successfully, else false.
 */
static bool GetUserId(char *userId)
{
	FlowMemoryManager memoryManager = FlowMemoryManager_New();

	if (memoryManager)
	{
		FlowDevice device = FlowClient_GetLoggedInDevice(memoryManager);
		if (device)
		{
			FlowID temp;

			temp = FlowUser_GetUserID(FlowDevice_RetrieveOwner(device));
			strcpy(userId, temp);
			FlowMemoryManager_Free(&memoryManager);
			return true;
		}
		else
		{
			LOG(LOG_ERR, "Failed to get logged in device");
		}
		FlowMemoryManager_Free(&memoryManager);
	}
	else
	{
		LOG(LOG_ERR, "Failed to create memory manager");
	}
	return false;
}

/**
 * @brief Send a flow message to user.
 * @param *message pointer to a message for flow user.
 * @return true if sending message to user is successful, else false.
 */
bool SendMessage(char *message)
{
	char userId[MAX_SIZE];

	GetUserId(userId);

	FlowMemoryManager memoryManager = FlowMemoryManager_New();

	if (memoryManager)
	{
		if (FlowMessaging_SendMessageToUser((FlowID)userId,
												"text/plain",
												message,
												strlen(message),
												MESSAGE_EXPIRY_TIMEOUT))
		{
			LOG(LOG_INFO, "Message sent to user = %s",message);
			FlowMemoryManager_Free(&memoryManager);
			return true;
		}
		else
		{
			LOG(LOG_ERR, "Failed to send message to user");
		}
		FlowMemoryManager_Free(&memoryManager);
	}
	else
	{
		LOG(LOG_ERR, "Failed to create memory manager");
	}
	return false;
}

/**
 * @brief Initialize libflow and register as a device.
 * @return true if device registration is successful else false.
 */
bool InitializeAndRegisterFlowDevice(void)
{
	RegistrationData regData;

	if (GetConfigData(&regData))
	{
		if (InitialiseLibFlow(regData.url, regData.key, regData.secret, regData.rememberMeToken))
		{
			if (FlowClient_IsDeviceLoggedIn())
			{
				LOG(LOG_INFO, "Device registration successful");
				return true;
			}
			else
			{
				LOG(LOG_ERR, "Failed to login as device");
			}
		}
		else
		{
			LOG(LOG_ERR, "Flow Core initialization failed");
		}
	}
	return false;
}

