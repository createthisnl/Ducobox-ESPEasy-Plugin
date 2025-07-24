//#################################### Plugin 194: DUCO Serial Box Sensors ##################################
//
//  DUCO Serial Gateway to read out the exernal installed Box Sensors
//
//  https://github.com/arnemauer/Ducobox-ESPEasy-Plugin
//  http://arnemauer.nl/ducobox-gateway/
//#######################################################################################################

// TODO: add a device parser for device droplist.

#include "_Plugin_Helper.h"

#define PLUGIN_194
#define PLUGIN_ID_194           194
#define PLUGIN_NAME_194         "DUCO Serial GW - External CO2 Sensor (CO2 & Temperature)"
#define PLUGIN_READ_TIMEOUT_194   1500 // DUCOBOX askes "live" CO2 sensor info, so answer takes sometimes a second.
#define PLUGIN_LOG_PREFIX_194   String("[P194] DUCO Ext. CO2 Sensor: ")

boolean Plugin_194_init = false;
// when calling 'PLUGIN_READ', if serial port is in use set this flag and check in PLUGIN_ONCE_A_SECOND if serial port is free.
bool P194_waitingForSerialPort[TASKS_MAX];

// a duco temp/humidity sensor can report the two values to the same IDX (domoticz)
// a duco CO2 sensor reports CO2 PPM and temperature. each needs an own IDX (domoticz)

typedef enum {
   DUCO_DATA_EXT_SENSOR_TEMP = 0,
   DUCO_DATA_EXT_SENSOR_RH = 1,
   DUCO_DATA_EXT_SENSOR_CO2_PPM = 2,
} DucoExternalSensorDataTypes;

typedef enum {
    P194_CONFIG_DEVICE = 0,
    P194_CONFIG_NODE_ADDRESS = 1,
    P194_CONFIG_LOG_SERIAL = 2,
} P194PluginConfigs;

typedef enum {
    P194_DUCO_DEVICE_NA = 0,
    P194_DUCO_DEVICE_CO2 = 1,
    P194_DUCO_DEVICE_CO2_TEMP = 2,
    P194_DUCO_DEVICE_RH = 3,
} P194DucoSensorDeviceTypes;

typedef enum {
    P194_DUCO_PARAMETER_TEMP = 73,
    P194_DUCO_PARAMETER_CO2 = 74,
    P194_DUCO_PARAMETER_RH = 75,
} P194DucoParameters;

boolean Plugin_194(byte function, struct EventStruct *event, String& string){
	boolean success = false;

   	switch (function){
		case PLUGIN_DEVICE_ADD:{
			Device[++deviceCount].Number = PLUGIN_ID_194;
			Device[deviceCount].Type = DEVICE_TYPE_DUMMY;
			Device[deviceCount].VType = Sensor_VType::SENSOR_TYPE_SINGLE;
			Device[deviceCount].Ports = 0;
			Device[deviceCount].PullUpOption = false;
			Device[deviceCount].InverseLogicOption = false;
			Device[deviceCount].FormulaOption = false;
			Device[deviceCount].DecimalsOnly = true;
			Device[deviceCount].ValueCount = 1;
			Device[deviceCount].SendDataOption = true;
			Device[deviceCount].TimerOption = true;
			Device[deviceCount].GlobalSyncOption = true;
			break;
		}

		case PLUGIN_GET_DEVICENAME:{
			string = F(PLUGIN_NAME_194);
			break;
		}

		case PLUGIN_GET_DEVICEVALUENAMES:{
			if(PCONFIG(P194_CONFIG_DEVICE) == P194_DUCO_DEVICE_CO2){
				safe_strncpy(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR("CO2_PPM"), sizeof(ExtraTaskSettings.TaskDeviceValueNames[0]));
			}else if(PCONFIG(P194_CONFIG_DEVICE) == P194_DUCO_DEVICE_CO2_TEMP){
				safe_strncpy(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR("Temperature"), sizeof(ExtraTaskSettings.TaskDeviceValueNames[0]));
			}
			break;
		}

      	case PLUGIN_WEBFORM_LOAD:{
			String options[3];
			options[P194_DUCO_DEVICE_NA] = "";
			options[P194_DUCO_DEVICE_CO2] = F("CO2 Sensor (CO2 PPM)");
			options[P194_DUCO_DEVICE_CO2_TEMP] = F("CO2 Sensor (Temperature)");

			addHtml(F("<TR><TD>Sensor type:<TD>"));
			byte choice = PCONFIG(P194_CONFIG_DEVICE);
			addSelector(String(F("Plugin_194_DEVICE_TYPE")), 3, options, NULL, NULL, choice, true, true);
			addFormNumericBox(F("Sensor Node address"), F("Plugin_194_NODE_ADDRESS"), PCONFIG(P194_CONFIG_NODE_ADDRESS), 0, 5000);
			addFormCheckBox(F("Log serial messages to syslog"), F("Plugin154_log_serial"), PCONFIG(P194_CONFIG_LOG_SERIAL));

			success = true;
			break;
      	}

      	case PLUGIN_WEBFORM_SAVE:{
			PCONFIG(P194_CONFIG_DEVICE) = getFormItemInt(F("Plugin_194_DEVICE_TYPE"));

			if(PCONFIG(P194_CONFIG_DEVICE) != P194_DUCO_DEVICE_NA){
				PCONFIG(P194_CONFIG_NODE_ADDRESS) = getFormItemInt(F("Plugin_194_NODE_ADDRESS"));
				ZERO_FILL(ExtraTaskSettings.TaskDeviceValueNames[0]);
			}

			PCONFIG(P194_CONFIG_LOG_SERIAL) = isFormItemChecked(F("Plugin154_log_serial"));
			success = true;
			break;
		}
      
      	case PLUGIN_EXIT:{
			if(serialPortInUseByTask == event->TaskIndex){
				serialPortInUseByTask = 255;
			}
			String log = PLUGIN_LOG_PREFIX_194;
			log += F("EXIT PLUGIN_194");
			addLogMove(LOG_LEVEL_INFO, log);
			clearPluginTaskData(event->TaskIndex); // clear plugin taskdata
			success = true;
			break;
		}


		case PLUGIN_INIT:{
			if(!Plugin_194_init && !ventilation_gateway_disable_serial){
				Serial.begin(115200, SERIAL_8N1);
			}

			String log = PLUGIN_LOG_PREFIX_194;
			log += F("Init plugin done.");
			addLogMove(LOG_LEVEL_INFO, log);

			Plugin_194_init = true;
			P194_waitingForSerialPort[event->TaskIndex] = false;
			success = true;
			break;
		}

      	case PLUGIN_READ:{
        	if (Plugin_194_init && (PCONFIG(P194_CONFIG_NODE_ADDRESS) != 0) && !ventilation_gateway_disable_serial){
				String log;
				if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
					log = PLUGIN_LOG_PREFIX_194;
					log += F("start read, eventid:");
					log += event->TaskIndex;
					addLogMove(LOG_LEVEL_DEBUG, log);
				}

	         	// check if serial port is in use by another task, otherwise set flag.
			   	if(serialPortInUseByTask == 255){
					serialPortInUseByTask = event->TaskIndex;

					if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
						log = PLUGIN_LOG_PREFIX_194;
						log += F("Read external CO2 sensor.");
						addLogMove(LOG_LEVEL_DEBUG, log);
					}

					if(PCONFIG(P194_CONFIG_DEVICE) == P194_DUCO_DEVICE_CO2){
						startReadExternalSensors(PLUGIN_LOG_PREFIX_194, DUCO_DATA_EXT_SENSOR_CO2_PPM, PCONFIG(P194_CONFIG_NODE_ADDRESS));
					}else if(PCONFIG(P194_CONFIG_DEVICE) == P194_DUCO_DEVICE_CO2_TEMP){
						startReadExternalSensors(PLUGIN_LOG_PREFIX_194, DUCO_DATA_EXT_SENSOR_TEMP, PCONFIG(P194_CONFIG_NODE_ADDRESS));
					}else{
						serialPortInUseByTask = 255; // no device type, stop reading.
					}

           		}else{
					if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
						char serialPortInUse[40];
						snprintf(serialPortInUse, sizeof(serialPortInUse)," %u, set flag to read data later.", serialPortInUseByTask);
						log = PLUGIN_LOG_PREFIX_194;
						log += F("Serial port in use by taskid");
						log += serialPortInUse;
						addLogMove(LOG_LEVEL_DEBUG, log);
					}

               		P194_waitingForSerialPort[event->TaskIndex] = true;
			   	}
         	}
			success = true;
			break;
      	}

      	case PLUGIN_ONCE_A_SECOND:{
			if(!ventilation_gateway_disable_serial){
				if(P194_waitingForSerialPort[event->TaskIndex]){
					if(serialPortInUseByTask == 255){
						Plugin_194(PLUGIN_READ, event, string);
						P194_waitingForSerialPort[event->TaskIndex] = false;
					}
				}

				if(serialPortInUseByTask == event->TaskIndex){
					if( (millis() - ducoSerialStartReading) > PLUGIN_READ_TIMEOUT_194){
						if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
							String log = PLUGIN_LOG_PREFIX_194;
							log += F("Serial reading timeout");
							addLogMove(LOG_LEVEL_DEBUG, log);
						}
						DucoTaskStopSerial(PLUGIN_LOG_PREFIX_194);
					}
				}
			}
			success = true;
			break;
      	}

      	case PLUGIN_SERIAL_IN: {
         	// if we unexpectedly receive data we need to flush and return success=true so espeasy won't interpret it as an serial command.
			if(serialPortInUseByTask == 255){
				DucoSerialFlush();
				success = true;
			}

        	if(serialPortInUseByTask == event->TaskIndex){
            	uint8_t result =0;
            	bool stop = false;
 				bool receivedNewValue = false;
           
            	while( (result = DucoSerialInterrupt()) != DUCO_MESSAGE_FIFO_EMPTY && stop == false){
               		switch(result){
                  		case DUCO_MESSAGE_ROW_END: {
                     		if(PCONFIG(P194_CONFIG_DEVICE) == P194_DUCO_DEVICE_CO2){
                        		receivedNewValue = readExternalSensorsProcessRow(PLUGIN_LOG_PREFIX_194, event->BaseVarIndex, DUCO_DATA_EXT_SENSOR_CO2_PPM, PCONFIG(P194_CONFIG_NODE_ADDRESS), PCONFIG(P194_CONFIG_LOG_SERIAL));
                     		}else if(PCONFIG(P194_CONFIG_DEVICE) == P194_DUCO_DEVICE_CO2_TEMP){
                        		receivedNewValue = readExternalSensorsProcessRow(PLUGIN_LOG_PREFIX_194, event->BaseVarIndex, DUCO_DATA_EXT_SENSOR_TEMP, PCONFIG(P194_CONFIG_NODE_ADDRESS), PCONFIG(P194_CONFIG_LOG_SERIAL));
                     		}
							if(receivedNewValue) sendData(event);
                     		duco_serial_bytes_read = 0; // reset bytes read counter
                     		break;
                  		}
						case DUCO_MESSAGE_END: {
							DucoThrowErrorMessage(PLUGIN_LOG_PREFIX_194, result);
							DucoTaskStopSerial(PLUGIN_LOG_PREFIX_194);
							stop = true;
							break;
                  		}
               		}
            	}
			   	success = true;
		   	}
			break;
	  	}

      	case PLUGIN_FIFTY_PER_SECOND: {
			if(serialPortInUseByTask == event->TaskIndex){
				if(serialSendCommandInProgress){
            		DucoSerialSendCommand(PLUGIN_LOG_PREFIX_194);
				}
			}
			success = true;
			break;
  		}

	}
  	return success;
}



   /*
    * Read external sensor information; this could contain CO2, Temperature and
    * Relative Humidity values, depending on the installed external sensors.
    */
        // SEND COMMAND: nodeparaget <Node> 74
void startReadExternalSensors(String logPrefix, uint8_t dataType, int nodeAddress){
	char logBuf[65];
	char command[20] = ""; /* 17 bytes + max 2 byte nodenumber + \r\n */
   	uint8_t parameter;
	char dataTypeName[5] = "";

	if(dataType == DUCO_DATA_EXT_SENSOR_TEMP){ 
		safe_strncpy(dataTypeName, "temp", sizeof(dataTypeName));
		parameter = P194_DUCO_PARAMETER_TEMP;
	}else if(dataType == DUCO_DATA_EXT_SENSOR_RH){ 
		safe_strncpy(dataTypeName, "RH", sizeof(dataTypeName));
		parameter = P194_DUCO_PARAMETER_RH;
	}else	if(dataType == DUCO_DATA_EXT_SENSOR_CO2_PPM){ 
		safe_strncpy(dataTypeName, "CO2", sizeof(dataTypeName));
		parameter = P194_DUCO_PARAMETER_CO2;
	}else{ 
		return;
	}

	snprintf(logBuf, sizeof(logBuf), "Start read external sensor. NodeAddress: %u. Type: %s", nodeAddress, dataTypeName);
	String log = PLUGIN_LOG_PREFIX_194;
	log += logBuf;
	addLogMove(LOG_LEVEL_INFO, log);

   	// set this variables before sending command
	ducoSerialStartReading = millis();
	duco_serial_bytes_read = 0; // reset bytes read counter
	duco_serial_rowCounter = 0; // reset row counter

   	// SEND COMMAND
   	snprintf_P(command, sizeof(command), "nodeparaget %d %d\r\n", nodeAddress, parameter);
	DucoSerialStartSendCommand(command);
}

                /* Example output:
                  // temperature
                   > nodeparaget 2 73
                    Get PARA 73 of NODE 2
                    --> 216
                    Done
                    
                  // CO2
                  > nodeparaget 2 74
                    Get PARA 74 of NODE 2
                    --> 492
                    Done

                  // relative humidity
                  > nodeparaget 2 75
                     Get PARA 75 of NODE 2
                     --> 5334
                     Done

                 */
bool readExternalSensorsProcessRow(String logPrefix, uint8_t userVarIndex, uint8_t dataType, int nodeAddress, bool serialLoggingEnabled){
	// get the first row to check for command, skip row 2 & 3, get row 4 (columnnames)
	String log;
   	if(serialLoggingEnabled){	     
		log = logPrefix;
		log += F("ROW: ");
		log += duco_serial_rowCounter;
		log += F(" bytes read:");
		log += duco_serial_bytes_read;
		addLogMove(LOG_LEVEL_DEBUG, log);     
		DucoSerialLogArray(logPrefix, duco_serial_buf, duco_serial_bytes_read, 0);
   	}

	if(duco_serial_rowCounter == 1){
    	uint8_t parameter;
	   	char command[20] = ""; /* 17 bytes + max 2 byte nodenumber + \r\n */

		switch(dataType){
			case DUCO_DATA_EXT_SENSOR_TEMP:{ parameter = P194_DUCO_PARAMETER_TEMP; break; }
			case DUCO_DATA_EXT_SENSOR_RH:{ parameter = P194_DUCO_PARAMETER_RH; break; }
			case DUCO_DATA_EXT_SENSOR_CO2_PPM:{ parameter = P194_DUCO_PARAMETER_CO2; break; }
			default: { return false; break; }
		}
         
      	snprintf_P(command, sizeof(command), "nodeparaget %d %d", nodeAddress, parameter);

		if (DucoSerialCheckCommandInResponse(logPrefix, command) ) {
			if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
				log = logPrefix;
				log += F("Command confirmed by ducobox");
				addLogMove(LOG_LEVEL_DEBUG, log);
			}
      	} else {
			if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
				log = logPrefix;
				log += F("Received invalid response");
				addLogMove(LOG_LEVEL_DEBUG, log);
			}
			DucoTaskStopSerial(logPrefix);
         	return false;
      	}
	}else if(duco_serial_rowCounter > 2){

		duco_serial_buf[duco_serial_bytes_read] = '\0';
		unsigned int raw_value;
		char logBuf[30];

     	if (sscanf((const char*)duco_serial_buf, "  --> %u", &raw_value) == 1) {
         	if(dataType == DUCO_DATA_EXT_SENSOR_CO2_PPM){
				unsigned int co2_ppm = raw_value; /* No conversion required */
				UserVar[userVarIndex] = co2_ppm;
				snprintf(logBuf, sizeof(logBuf), "CO2 PPM: %u = %u PPM", raw_value, co2_ppm);
         	}else if(dataType == DUCO_DATA_EXT_SENSOR_TEMP){
				float temp = (float) raw_value / 10.;
				UserVar[userVarIndex] = temp;
				snprintf(logBuf, sizeof(logBuf), "TEMP: %u = %.1fÂ°C", raw_value, temp);
         	}else if(dataType == DUCO_DATA_EXT_SENSOR_RH){
				float rh = (float) raw_value / 100.;
				UserVar[userVarIndex] = rh;
				snprintf(logBuf, sizeof(logBuf), "RH: %u = %.2f%%", raw_value, rh);
         	}
         	log = logPrefix;
			log += logBuf;
         	addLogMove(LOG_LEVEL_INFO, log);
			return true;
      	}
   	} 
	return false;
} // end of readExternalSensors()

