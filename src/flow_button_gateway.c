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
 * @file flow_button_gateway.c
 * @brief Flow button gateway application at first waits for device to get provisioned, and then
 *        starts polling button presses on constrained device and set the led on another. Also send
 *        flow messages to user for change in LED state. It uses FlowDeviceManagment Server SDK for
 *        communicating with lwm2m client on constrained devices and Client SDK for communicating
 *        with lwm2m server on flowcloud.
 */

/***************************************************************************************************
 * Includes
 **************************************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "awa/server.h"
#include "awa/client.h"
#include "flow_interface.h"
#include "flow/core/flow_time.h"
#include "flow/core/flow_memalloc.h"
#include "log.h"

/***************************************************************************************************
 * Definitions
 **************************************************************************************************/

/** Calculate size of array. */
#define ARRAY_SIZE(x) ((sizeof x) / (sizeof *x))

//! @cond Doxygen_Suppress
#define IPC_SERVER_PORT			(54321)
#define IPC_CLIENT_PORT			(12345)
#define IP_ADDRESS				"127.0.0.1"
#define BUTTON_STR			"Counter"
#define LED_STR				"On/Off"
#define FLOW_ACCESS_OBJECT_ID		(20001)
#define FLOW_OBJECT_INSTANCE_ID		(0)
#define ON_STR				"on"
#define OFF_STR				"off"
#define BUTTON_OBJECT_ID	(3200)
#define BUTTON_RESOURCE_ID	(5501)
#define LED_OBJECT_ID		(3311)
#define LED_RESOURCE_ID		(5850)
#define LED_RESOURCE_PATH	"/3311/0"
#define MIN_INSTANCES     (0)
#define MAX_INSTANCES     (1)
#define OPERATION_TIMEOUT	(5000)
#define URL_PATH_SIZE		(16)
#define FLOW_SERVER_CONNECT_TRIALS	(5)
//! @endcond

/***************************************************************************************************
 * Typedef
 **************************************************************************************************/

/**
 * A structure to contain resource information.
 */
typedef struct
{
	/*@{*/
	AwaResourceID id; /**< resource ID */
	AwaResourceInstanceID instanceID; /**< resource instance ID */
	AwaResourceType type; /**< type of resource e.g. bool, string, integer etc. */
	const char *name; /**< resource name */
	/*@}*/
}RESOURCE_T;

/**
 * A structure to contain objects information.
 */
typedef struct
{
	/*@{*/
	char *clientID; /**< client ID */
	AwaObjectID id; /**< object ID */
	AwaObjectInstanceID instanceID; /**< object instance ID */
	const char *name; /**< object name */
	unsigned int numResources; /**< number of resource under this object */
	RESOURCE_T *resources; /**< resource information */
	/*@}*/
}OBJECT_T;

/***************************************************************************************************
 * Globals
 **************************************************************************************************/

/** Variable storing device registration status. */
static bool isDeviceRegistered = false;
/** Set default debug level to info. */
int debugLevel = LOG_INFO;
/** Set default debug stream to NULL. */
FILE *debugStream = NULL;

/** Initializing objects. */
static OBJECT_T objects[] =
{
	{
		"ButtonDevice",
		BUTTON_OBJECT_ID,
		0,
		"DigitalInput",
		1,
		(RESOURCE_T []){
							{
								BUTTON_RESOURCE_ID,
								0,
								AwaResourceType_Integer,
								BUTTON_STR
							},
						}
	},
	{
		"LedDevice",
		LED_OBJECT_ID,
		0,
		"LightControl",
		1,
		(RESOURCE_T []){
							{
								LED_RESOURCE_ID,
								0,
								AwaResourceType_Boolean,
								LED_STR
							},
						}
	},
};

/***************************************************************************************************
 * Implementation
 **************************************************************************************************/

/**
 * @brief Update heartbeat led status to on/off.
 * @param status led status.
 */
static void SetHeartbeatLed(bool status)
{
	int tmp = 0;

	if (status)
	{
		tmp = system("/usr/bin/set_led.sh 1");
	}
	else
	{
		tmp = system("/usr/bin/set_led.sh 0");
	}

	if (tmp != 0)
	{
		LOG(LOG_WARN, "Setting heartbeat led failed.");
	}
}

/**
 * @brief Prints flow_button_gateway_appd usage.
 * @param *program holds application name.
 */
static void PrintUsage(const char *program)
{
	printf("Usage: %s [options]\n\n"
			" -l : Log filename.\n"
			" -v : Debug level from 1 to 5\n"
			"      fatal(1), error(2), warning(3), info(4), debug(5) and max(>5)\n"
			"      default is info.\n"
			" -h : Print help and exit.\n\n",
			program);
}

/**
 * @brief Parses command line arguments passed to flow_button_gateway_appd.
 * @return -1 in case of failure, 0 for printing help and exit, and 1 for success.
 */
static int ParseCommandArgs(int argc, char *argv[], const char **fptr)
{
	int opt, tmp;
	opterr = 0;

	while (1)
	{
		opt = getopt(argc, argv, "l:v:");
		if (opt == -1)
		{
			break;
		}

		switch (opt)
		{
			case 'l':
				*fptr = optarg;
				break;
			case 'v':
				tmp = strtoul(optarg, NULL, 0);
				if (tmp >= LOG_FATAL && tmp <= LOG_DBG)
				{
					debugLevel = tmp;
				}
				else
				{
					LOG(LOG_ERR, "Invalid debug level");
					PrintUsage(argv[0]);
					return -1;
				}
				break;
			case 'h':
				PrintUsage(argv[0]);
				return 0;
			default:
				PrintUsage(argv[0]);
				return -1;
		}
	}

	return 1;
}

/**
 * @brief Construct a flow message, depending on ledState, and send it to user.
 *        This is done using FlowMessaging SDK apis.
 * @param ledState holds led's states whether led is on or off.
 * @return true if construction and sending of flow message is successful, else false.
 */
static bool ConstructAndSendFlowMessage(const bool ledState)
{
	char *data = NULL;
	bool success = true;
	unsigned int msgSize = 0, strSize = 0;
	char msgStr[] = "%02d:%02d:%02d %02d-%02d-%04d LED %s";

	strSize = ledState ? strlen(ON_STR) : strlen(OFF_STR);
	msgSize = strlen(msgStr) + strSize  + 1;

	data = (char *)Flow_MemAlloc(msgSize);

	if (data)
	{
		time_t time;
		Flow_GetTime(&time);
		struct tm timeNow;
		gmtime_r(&time, &timeNow);

		snprintf(data, msgSize, msgStr,
				timeNow.tm_hour,
				timeNow.tm_min,
				timeNow.tm_sec,
				timeNow.tm_mday,
				timeNow.tm_mon + 1,
				timeNow.tm_year + 1900,
				ledState?ON_STR:OFF_STR);

		success = SendMessage(data);
		Flow_MemFree((void **)&data);
	}
	else
	{
		success = false;
	}
	return success;
}


/**
 * @brief Checks whether flow access object is registerd or not,
 *        which shows the privisioning status of device.
 * @param *session holds client session.
 * @return true if successfully provisioned, else false.
 */
static bool WaitForProvisioning(AwaClientSession *session)
{
	bool success = false;

	AwaClientGetOperation *operation = AwaClientGetOperation_New(session);
	char instancePath[URL_PATH_SIZE] = {0};

	if (operation != NULL)
	{
		if (AwaAPI_MakeObjectInstancePath(instancePath,
												URL_PATH_SIZE,
												FLOW_ACCESS_OBJECT_ID, 0) == AwaError_Success)
		{
			if (AwaClientGetOperation_AddPath(operation, instancePath) == AwaError_Success)
			{
				if (AwaClientGetOperation_Perform(operation,
														OPERATION_TIMEOUT) == AwaError_Success)
				{
					const AwaClientGetResponse *response = NULL;
					response = AwaClientGetOperation_GetResponse(operation);
					if (response)
					{
						if (AwaClientGetResponse_ContainsPath(response, instancePath))
						{
							LOG(LOG_INFO, "Gateway is provisioned.\n");
							success = true;
						}
					}
				}
			}
		}
		AwaClientGetOperation_Free(&operation);
	}
	return success;
}

/**
 * @brief Checks whether Led object is defined or not on client server.
 * @param *session holds client session.
 * @return true if object is already defined, else false.
 */
bool IsLedObjectDefined(const AwaClientSession *session)
{
	AwaClientGetOperation *operation = AwaClientGetOperation_New(session);
	char instancePath[URL_PATH_SIZE] = {0};
	bool success = false;

	if (operation != NULL)
	{
		if (AwaAPI_MakeObjectInstancePath(instancePath,
												URL_PATH_SIZE,
												LED_OBJECT_ID, 0) == AwaError_Success)
		{
			if (AwaClientGetOperation_AddPath(operation, instancePath) == AwaError_Success)
			{
				if (AwaClientGetOperation_Perform(operation,
														OPERATION_TIMEOUT) == AwaError_Success)
				{
					const AwaClientGetResponse *response = NULL;
					response = AwaClientGetOperation_GetResponse(operation);
					if (response)
					{
						if (AwaClientGetResponse_ContainsPath(response, instancePath))
						{
							success = true;
						}
					}
				}
			}
		}
		AwaClientGetOperation_Free(&operation);
	}
	return success;
}

/**
 * @brief Set Led resource on AwaLWM2M client.
 * @param *session holds client session.
 * @param value resource value to set.
 * @return true if setting resource value is successful, else false.
 */
static bool SetLedResource(const AwaClientSession *session, const bool value)
{
	AwaClientSetOperation *operation = NULL;
	char ledResourcePath[URL_PATH_SIZE] = {0};
	bool success = false;
	AwaError error;

	if (AwaAPI_MakeResourcePath(ledResourcePath,
										URL_PATH_SIZE,
										LED_OBJECT_ID, 0, LED_RESOURCE_ID) != AwaError_Success)
	{
		LOG(LOG_INFO, "Couldn't generate object and resource path for LED.");
		return false;
	}

	operation = AwaClientSetOperation_New(session);
	if (operation != NULL)
	{
		if (AwaClientSetOperation_CreateOptionalResource(operation,
														ledResourcePath) == AwaError_Success)
		{
			if (!IsLedObjectDefined(session))
			{
				AwaClientSetOperation_CreateObjectInstance(operation, LED_RESOURCE_PATH);
			}

			if (AwaClientSetOperation_AddValueAsBoolean(operation,
																ledResourcePath,
																value) == AwaError_Success)
			{
				if ((error = AwaClientSetOperation_Perform(operation,
														OPERATION_TIMEOUT)) == AwaError_Success)
				{
					success = true;
					LOG(LOG_INFO, "Set %d on client.\n",value);
				}
				else
				{
					LOG(LOG_ERR, "AwaClientSetOperation_Perform failed\n"
														"error: %s", AwaError_ToString(error));
				}
			}
		}
		AwaClientSetOperation_Free(&operation);
	}
	return success;
}

/**
 * @brief Checks if resource is defined on server or not.
 * @param *session holds server session.
 * @param *path full path of resource to be searched.
 * @return true if resource is defined on server, else false.
 */
static bool IsResourceDefined(const AwaServerSession *session, const char *path)
{
	AwaObjectID objectID;
	AwaResourceID resourceID;
	AwaError error;
	const AwaResourceDefinition *resourceDefinition = NULL;

	if ((error = AwaServerSession_PathToIDs(session,
										path,
										&objectID,
										NULL,
										&resourceID)) == AwaError_Success)
	{
		const AwaObjectDefinition *objectDefinition = NULL;
		objectDefinition = AwaServerSession_GetObjectDefinition(session, objectID);
		if (objectDefinition != NULL)
		{
			resourceDefinition = AwaObjectDefinition_GetResourceDefinition(objectDefinition,
																				resourceID);
		}
		else
		{
			LOG(LOG_ERR, "objectDefinition is NULL\n");
		}
	}
	else
	{
		LOG(LOG_ERR, "AwaServerSession_PathToIDs() failed\n"
														"error: %s", AwaError_ToString(error));
	}
	return (resourceDefinition != NULL);
}

/**
 * @brief Update led resource value on server.
 * @param *session holds server session.
 * @param value resource value to write.
 * @return true if writing resource value is successful, else false.
 */
static bool WriteLedResource(const AwaServerSession *session, const bool value)
{
	char ledResourcePath[URL_PATH_SIZE] = {0};
	bool success = false;
	AwaError error;

	AwaServerWriteOperation *operation = NULL;
	operation = AwaServerWriteOperation_New(session, AwaWriteMode_Update);

	if (AwaAPI_MakeResourcePath(ledResourcePath,
										URL_PATH_SIZE,
										LED_OBJECT_ID, 0, LED_RESOURCE_ID) != AwaError_Success)
	{
		LOG(LOG_INFO, "Couldn't generate all object and resource paths.\n");
		return false;
	}

	if (operation != NULL)
	{
		if (IsResourceDefined(session, ledResourcePath))
		{
			if (AwaServerWriteOperation_AddValueAsBoolean(operation,
																ledResourcePath,
																value) == AwaError_Success)
			{
				if ((error = AwaServerWriteOperation_Perform(operation,
														"LedDevice",
														OPERATION_TIMEOUT)) == AwaError_Success)
				{
					LOG(LOG_INFO, "Written %d to server.\n", value);
					success = true;
				}
				else
				{
					LOG(LOG_ERR, "AwaServerWriteOperation_Perform failed\n"
														"error: %s", AwaError_ToString(error));
				}
			}
		}
		AwaServerWriteOperation_Free(&operation);
	}
	return success;
}

/**
 * @brief Update led resource value on client and server both, and also send flow message to user.
 * @param *clientSession holds client session.
 * @param *serverSession holds server session.
 * @param buttonState button resource value to update.
 */
void PerformUpdate(const AwaClientSession *clientSession,
						const AwaServerSession *serverSession,
						const bool buttonState)
{
	if (!WriteLedResource(serverSession, buttonState))
	{
		LOG(LOG_ERR, "Writing to LED resource on server failed.\n");
	}

	if (!SetLedResource(clientSession, buttonState))
	{
		LOG(LOG_ERR, "Setting to LED resource on client failed.\n");
	}

	if (isDeviceRegistered)
	{
		if (ConstructAndSendFlowMessage(buttonState) == false)
		{
			LOG(LOG_ERR, "Flow message send failed");
		}
	}
}

/**
 * @brief Poll button status on server and call for update in case of changes.

 * @param *clientSession holds client session.
 * @param *serverSession holds server session.
 * @return Ideally this function should never exit and should keep on running in while,
 *        however, there are some exit points, and out of those, there is one which is
 *        forcing the calling function to call it again after some initializations.
 */
static bool StartPollingButtonstate(const AwaClientSession *clientSession,
										const AwaServerSession *serverSession)
{
	AwaServerReadOperation *operation = NULL;
	char buttonResourcePath[URL_PATH_SIZE] = {0};
	AwaError error = AwaError_Unspecified;
	int cachedState;

	operation = AwaServerReadOperation_New(serverSession);
	if (operation == NULL)
	{
		LOG(LOG_INFO, "Read operation on server failed.\n");
		return false;
	}

	if (AwaAPI_MakeResourcePath(buttonResourcePath,
										URL_PATH_SIZE,
										BUTTON_OBJECT_ID,
										0, BUTTON_RESOURCE_ID) != AwaError_Success)
	{
		LOG(LOG_INFO, "Couldn't generate all object and resource paths.\n");
		return false;
	}

	error = AwaServerReadOperation_AddPath(operation, "ButtonDevice", buttonResourcePath);
	if (error == AwaError_Success)
	{
		while (true)
		{
			error = AwaServerReadOperation_Perform(operation, OPERATION_TIMEOUT);
			if (error == AwaError_Success)
			{
				const AwaServerReadResponse *readResponse = NULL;
				readResponse = AwaServerReadOperation_GetResponse(operation, "ButtonDevice");
				if (readResponse != NULL)
				{
					const AwaInteger *value = NULL;

					AwaServerReadResponse_GetValueAsIntegerPointer(readResponse,
																		buttonResourcePath,
																		&value);
					if ((value != NULL) && (cachedState != *value))
					{
						bool temp = *value % 2;
						PerformUpdate(clientSession, serverSession, temp);
						cachedState = *value;
					}
				}
				else
				{
					LOG(LOG_ERR, "AwaServerReadOperation_GetResponse failed");
					return false;
				}
				SetHeartbeatLed(false);
				sleep(1);
				SetHeartbeatLed(true);
			}
			else
			{
				LOG(LOG_ERR, "AwaServerReadOperation_Perform failed\n"
														"error: %s", AwaError_ToString(error));
				return true;
			}
		}
	}
	return false;
}

/**
 * @brief Check to see if a constrained device by the name endPointName has registered
 *        itself with the server on the gateway or not.
 * @param *session holds server session.
 * @param *endPointName holds client name.
 * @return true if constrained device is in client list i.e. registered, else false.
 */
static bool CheckConstrainedRegistered(const AwaServerSession *session, const char *endPointName)
{
	bool success = false;
	AwaError error;

	AwaServerListClientsOperation *operation = AwaServerListClientsOperation_New(session);
	if (operation != NULL)
	{
		if ((error = AwaServerListClientsOperation_Perform(operation,
														OPERATION_TIMEOUT)) == AwaError_Success)
		{
			AwaClientIterator *clientIterator = NULL;
			clientIterator = AwaServerListClientsOperation_NewClientIterator(operation);
			if (clientIterator != NULL)
			{
				while(AwaClientIterator_Next(clientIterator))
				{
					const char *clientID = AwaClientIterator_GetClientID(clientIterator);

					if (!strcmp(endPointName, clientID))
					{
						LOG(LOG_INFO, "Constrained device %s registered", endPointName);
						success = true;
						break;
					}
				}
				AwaClientIterator_Free(&clientIterator);
			}
			else
			{
				LOG(LOG_ERR, "AwaServerListClientsOperation_NewClientIterator failed");
			}
		}
		else
		{
			LOG(LOG_ERR, "AwaServerListClientsOperation_Perform failed\n"
														"error: %s", AwaError_ToString(error));
		}

		if ((error = AwaServerListClientsOperation_Free(&operation)) != AwaError_Success)
		{
			LOG(LOG_ERR, "AwaServerListClientsOperation_Free failed\n"
														"error: %s", AwaError_ToString(error));
		}
	}
	else
	{
		LOG(LOG_ERR, "AwaServerListClientsOperation_New failed");
	}
	return success;
}

/**
 * @brief Add all resource definitions belongs to object.
 * @param *object whose resources are to be defined.
 * @return pointer to flow object definition.
 */
static AwaObjectDefinition *AddResourceDefinitions(OBJECT_T *object)
{
	int i;

	AwaObjectDefinition *objectDefinition = AwaObjectDefinition_New(object->id,
		object->name, MIN_INSTANCES, MAX_INSTANCES);
	if (objectDefinition != NULL)
	{
		// define resources
		for (i = 0; i < object->numResources; i++)
		{
			if (object->resources[i].type == AwaResourceType_Integer)
			{
				if( AwaObjectDefinition_AddResourceDefinitionAsInteger(
																objectDefinition,
																object->resources[i].id,
																object->resources[i].name,
																true,
																AwaResourceOperations_ReadWrite,
																0) != AwaError_Success)
				{
					LOG(LOG_ERR,
							"Could not add resource definition (%s [%d]) to object definition.",
							object->resources[i].name,
							object->resources[i].id);
					AwaObjectDefinition_Free(&objectDefinition);
				}
			}
			else if (object->resources[i].type == AwaResourceType_Boolean)
			{
				if( AwaObjectDefinition_AddResourceDefinitionAsBoolean(
																objectDefinition,
																object->resources[i].id,
																object->resources[i].name,
																true,
																AwaResourceOperations_ReadWrite,
																NULL) != AwaError_Success)
				{
					LOG(LOG_ERR,
							"Could not add resource definition (%s [%d]) to object definition.",
							object->resources[i].name,
							object->resources[i].id);
					AwaObjectDefinition_Free(&objectDefinition);
				}
			}

		}
	}
	return objectDefinition;
}

/**
 * @brief Define all objects and its resources with client daemon.
 * @param *session holds client session.
 * @return true if object is successfully defined on client, else false.
 */
bool DefineClientObjects(AwaClientSession *session)
{
	unsigned int i;
	unsigned int definitionCount = 0;
	bool success = true;

	LOG(LOG_INFO, "Defining flow objects on client");

	if (session == NULL)
	{
		LOG(LOG_ERR, "Null parameter passsed to %s()", __func__);
		return false;
	}

	AwaClientDefineOperation *handler = AwaClientDefineOperation_New(session);
	if (handler == NULL)
	{
		LOG(LOG_ERR, "Failed to create define operation for session on client");
		return false;
	}

	for (i = 0; (i < ARRAY_SIZE(objects)) && success; i++)
	{
		if (AwaClientSession_IsObjectDefined(session, objects[i].id))
		{
			LOG(LOG_DBG, "%s object already defined on client", objects[i].name);
			continue;
		}

		AwaObjectDefinition *objectDefinition = AddResourceDefinitions(&objects[i]);

		if (objectDefinition != NULL)
		{
			if (AwaClientDefineOperation_Add(handler, objectDefinition) != AwaError_Success)
			{
				LOG(LOG_ERR, "Failed to add object definition to define operation on client");
				success = false;
			}
			definitionCount++;
			AwaObjectDefinition_Free(&objectDefinition);
		}
	}

	if (success && definitionCount != 0)
	{
		if (AwaClientDefineOperation_Perform(handler, OPERATION_TIMEOUT) != AwaError_Success)
		{
			LOG(LOG_ERR, "Failed to perform define operation on client");
			success = false;
		}
	}
	if (AwaClientDefineOperation_Free(&handler) != AwaError_Success)
	{
		LOG(LOG_WARN, "Failed to free define operation object on client");
	}
	return success;
}

/**
 * @brief Define all objects and its resources with server deamon.
 * @param *session holds server session.
 * @return true if object is successfully defined on server, else false.
 */
bool DefineServerObjects(AwaServerSession *session)
{
	unsigned int i;
	unsigned int definitionCount = 0;
	bool success = true;

	LOG(LOG_INFO, "Defining flow objects on server");

	if (session == NULL)
	{
		LOG(LOG_ERR, "Null parameter passsed to %s()", __func__);
		return false;
	}

	AwaServerDefineOperation *handler = AwaServerDefineOperation_New(session);
	if (handler == NULL)
	{
		LOG(LOG_ERR, "Failed to create define operation for session on server");
		return false;
	}

	for (i = 0; (i < ARRAY_SIZE(objects)) && success; i++)
	{
		if (AwaServerSession_IsObjectDefined(session, objects[i].id))
		{
			LOG(LOG_DBG, "%s object already defined on server", objects[i].name);
			continue;
		}

		AwaObjectDefinition *objectDefinition = AddResourceDefinitions(&objects[i]);

		if (objectDefinition != NULL)
		{
			if (AwaServerDefineOperation_Add(handler, objectDefinition) != AwaError_Success)
			{
				LOG(LOG_ERR, "Failed to add object definition to define operation on server");
				success = false;
			}
			definitionCount++;
			AwaObjectDefinition_Free(&objectDefinition);
		}
	}

	if (success && definitionCount != 0)
	{
		if (AwaServerDefineOperation_Perform(handler, OPERATION_TIMEOUT) != AwaError_Success)
		{
			LOG(LOG_ERR, "Failed to perform define operation on server");
			success = false;
		}
	}
	if (AwaServerDefineOperation_Free(&handler) != AwaError_Success)
	{
		LOG(LOG_WARN, "Failed to free define operation object on server");
	}
	return success;
}

/**
 * @brief Create a fresh session with client.
 * @param port client's IPC port number.
 * @param *address ip address of client daemon.
 * @return pointer to client's session.
 */
AwaClientSession *Client_EstablishSession(unsigned int port, const char *address)
{
	/* Initialise Device Management session */
	AwaClientSession * session;
	session = AwaClientSession_New();

	if (session != NULL)
	{
		/* call set IPC as UDP, pass address and port */
		if (AwaClientSession_SetIPCAsUDP(session, address, port) == AwaError_Success)
		{
			if (AwaClientSession_Connect(session) != AwaError_Success)
			{
				LOG(LOG_ERR, "AwaClientSession_Connect() failed\n");
				AwaClientSession_Free(&session);
			}
		}
		else
		{
			LOG(LOG_ERR, "AwaClientSession_SetIPCAsUDP() failed\n");
			AwaClientSession_Free(&session);
		}
	}
	else
	{
		LOG(LOG_ERR, "AwaClientSession_New() failed\n");
	}
	return session;
}

/**
 * @brief Create a fresh session with server.
 * @param port server's IPC port number.
 * @param *address ip address of server daemon.
 * @return pointer to server's session.
 */
AwaServerSession *Server_EstablishSession(unsigned int port, const char *address)
{
	/* Initialise Device Management session */
	AwaServerSession * session;
	session = AwaServerSession_New();

	if (session != NULL)
	{
		/* call set IPC as UDP, pass address and port */
		if (AwaServerSession_SetIPCAsUDP(session, address, port) == AwaError_Success)
		{
			if (AwaServerSession_Connect(session) == AwaError_Success)
			{
				LOG(LOG_INFO, "Server session established\n");
			}
			else
			{
				LOG(LOG_ERR, "AwaServerSession_Connect() failed\n");
				AwaServerSession_Free(&session);
			}
		}
		else
		{
			LOG(LOG_ERR, "AwaServerSession_SetIPCAsUDP() failed\n");
			AwaServerSession_Free(&session);
		}
	}
	else
	{
		LOG(LOG_ERR, "AwaServerSession_New() failed\n");
	}
	return session;
}

/**
 * @brief Flow button gateway application to poll a button press on constrained device,
 *        and set the led on another. Also send a flow message to user for change in LED state.
 */
int main(int argc, char **argv)
{
	int i, ret;
	FILE *configFile;
	const char *fptr = NULL;

	ret = ParseCommandArgs(argc, argv, &fptr);
	if (ret <= 0)
	{
		return ret;
	}

	if (fptr)
	{
		configFile = fopen(fptr, "w");
		if (configFile != NULL)
		{
			debugStream  = configFile;
		}
		else
		{
			LOG(LOG_ERR, "Failed to create or open %s file", fptr);
		}
	}

	AwaClientSession *clientSession = NULL;
	AwaServerSession *serverSession = NULL;

	LOG(LOG_INFO, "Flow Button Gateway Application");
	LOG(LOG_INFO, "------------------------\n");

	clientSession = Client_EstablishSession(IPC_CLIENT_PORT, IP_ADDRESS);
	if (clientSession != NULL)
	{
		LOG(LOG_ERR, "Client session established\n");
	}

	serverSession = Server_EstablishSession(IPC_SERVER_PORT, IP_ADDRESS);
	if (serverSession == NULL)
	{
		LOG(LOG_ERR, "Failed to establish server session\n");
	}

	LOG(LOG_INFO, "Wait until device is provisioned\n");
	SetHeartbeatLed(true);

	while (!WaitForProvisioning(clientSession))
	{
		LOG(LOG_INFO, "Waiting...\n");
		AwaClientSession_Free(&clientSession);
		sleep(2);
		clientSession = Client_EstablishSession(IPC_CLIENT_PORT, IP_ADDRESS);
	}

	for (i = FLOW_SERVER_CONNECT_TRIALS; i > 0; i--)
	{
		isDeviceRegistered = InitializeAndRegisterFlowDevice();
		if (isDeviceRegistered)
		{
			break;
		}
		LOG(LOG_INFO, "Try to connect to Flow Server for %d more trials..\n", i);
		sleep(1);
	}

	if (DefineServerObjects(serverSession) && DefineClientObjects(clientSession))
	{
		for (i = 0; i < ARRAY_SIZE(objects); i++)
		{
			LOG(LOG_INFO, "Waiting for constrained device '%s' to be up",objects[i].clientID);
			while (CheckConstrainedRegistered(serverSession, objects[i].clientID) == false)
			{
				sleep(1 /*second*/);
			}
		}

		while (true)
		{
			if (StartPollingButtonstate(clientSession, serverSession))
			{
				if (AwaServerSession_Disconnect(serverSession) != AwaError_Success)
				{
					LOG(LOG_ERR, "Failed to disconnect server session");
				}

				if (AwaServerSession_Free(&serverSession) != AwaError_Success)
				{
					LOG(LOG_WARN, "Failed to free server session");
				}
				sleep(1);
				serverSession = Server_EstablishSession(IPC_SERVER_PORT, IP_ADDRESS);
			}
			else
			{
				break;
			}
		}
	}

	/* Should never come here */
	SetHeartbeatLed(false);

	if (AwaServerSession_Disconnect(serverSession) != AwaError_Success)
	{
		LOG(LOG_ERR, "Failed to disconnect server session");
	}

	if (AwaServerSession_Free(&serverSession) != AwaError_Success)
	{
		LOG(LOG_WARN, "Failed to free server session");
	}

	if (AwaClientSession_Disconnect(clientSession) != AwaError_Success)
	{
		LOG(LOG_ERR, "Failed to disconnect client session");
	}

	if (AwaClientSession_Free(&clientSession) != AwaError_Success)
	{
		LOG(LOG_WARN, "Failed to free client session");
	}

	LOG(LOG_INFO, "Flow Button Gateway Application Failure");

	return -1;
}

