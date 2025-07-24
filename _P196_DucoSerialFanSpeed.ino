//############################### Plugin 196: DUCO Serial GW - FanSpeed #################################
//
//  DUCO Serial Gateway to read out fanspeed 
//
//  https://github.com/arnemauer/Ducobox-ESPEasy-Plugin
//  http://arnemauer.nl/ducobox-gateway/
//#######################################################################################################

#include "_Plugin_Helper.h"

#define PLUGIN_196
#define PLUGIN_ID_196           196
#define PLUGIN_NAME_196         "DUCO Serial GW - FanSpeed"
#define PLUGIN_READ_TIMEOUT_196   1000 // DUCOBOX askes "live" CO2 sensor info, so answer takes sometimes a second.
#define PLUGIN_LOG_PREFIX_196   String("[P196] DUCO FanSpeed: ")

#define PLUGIN_VALUENAME1_196 "FanSpeed"

boolean Plugin_196_init = false;
// when calling 'PLUGIN_READ', if serial port is in use set this flag and check in PLUGIN_ONCE_A_SECOND if serial port is free.
bool P196_waitingForSerialPort = false;

typedef enum {
    P196_CONFIG_LOG_SERIAL = 0,
} P196PluginConfigs;

boolean Plugin_196(byte function, struct EventStruct *event, String& string){
	boolean success = false;

  	switch (function){
		case PLUGIN_DEVICE_ADD: {
      		Device[++deviceCount].Number = PLUGIN_ID_196;
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
			string = F(PLUGIN_NAME_196);
			break;
		}

		case PLUGIN_GET_DEVICEVALUENAMES:{
			strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_196));
      		break;
   		}

		case PLUGIN_WEBFORM_LOAD:{
			addFormCheckBox(F("Log serial messages to syslog"), F("Plugin156_log_serial"), PCONFIG(P196_CONFIG_LOG_SERIAL));
			success = true;
			break;
		}

		case PLUGIN_WEBFORM_SAVE:{
			PCONFIG(P196_CONFIG_LOG_SERIAL) = isFormItemChecked(F("Plugin156_log_serial"));
			success = true;
			break;
		}
      
		case PLUGIN_EXIT: {
			if(serialPortInUseByTask == event->TaskIndex){
				serialPortInUseByTask = 255;
			}
			String log = PLUGIN_LOG_PREFIX_196;
			log += F("EXIT PLUGIN_196");
			addLogMove(LOG_LEVEL_INFO, log);
			clearPluginTaskData(event->TaskIndex); // clear plugin taskdata
			success = true;
			break;
		}


		case PLUGIN_INIT:{
			if(!Plugin_196_init && !ventilation_gateway_disable_serial){
				Serial.begin(115200, SERIAL_8N1);
				String log = PLUGIN_LOG_PREFIX_196;
				log += F("Init plugin done.");
				addLogMove(LOG_LEVEL_INFO, log);
				Plugin_196_init = true;
			}

			success = true;
			break;
   		}

      	case PLUGIN_READ:{
         	if (Plugin_196_init && !ventilation_gateway_disable_serial){
				String log;

				if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
					log = PLUGIN_LOG_PREFIX_196;
					log += F("start read, eventid:");
					log += event->TaskIndex;
					addLogMove(LOG_LEVEL_DEBUG, log);
				}
				// check if serial port is in use by another task, otherwise set flag.
				if(serialPortInUseByTask == 255){
					serialPortInUseByTask = event->TaskIndex;

					if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
						log = PLUGIN_LOG_PREFIX_196;
						log += F("Start reading fanspeed");
						addLogMove(LOG_LEVEL_DEBUG, log);
					}
					Plugin_196_startReadFanSpeed(PLUGIN_LOG_PREFIX_196);
				}else{
					if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
						char serialPortInUse[40];
						snprintf(serialPortInUse, sizeof(serialPortInUse)," %u, set flag to read data later.", serialPortInUseByTask);
						log = PLUGIN_LOG_PREFIX_196;
						log += F("Serial port in use by taskid");
						log += serialPortInUse;
						addLogMove(LOG_LEVEL_DEBUG, log);		
					}		
					P196_waitingForSerialPort = true;
				}
         	}
	        success = true;
    	    break;
      	}

	   	case PLUGIN_ONCE_A_SECOND:{
			if(!ventilation_gateway_disable_serial){
				if(P196_waitingForSerialPort){
					if(serialPortInUseByTask == 255){
						Plugin_196(PLUGIN_READ, event, string);
						P196_waitingForSerialPort = false;
					}
				}

				if(serialPortInUseByTask == event->TaskIndex){
					if( (millis() - ducoSerialStartReading) > PLUGIN_READ_TIMEOUT_196){
						if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
							String log = PLUGIN_LOG_PREFIX_196;
							log += F("Serial reading timeout");
							addLogMove(LOG_LEVEL_DEBUG, log);
						}
						DucoTaskStopSerial(PLUGIN_LOG_PREFIX_196);
						serialPortInUseByTask = 255;
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
				uint8_t result = 0;
				bool stop = false;
				bool receivedNewValue = false;
				while( (result = DucoSerialInterrupt()) != DUCO_MESSAGE_FIFO_EMPTY && stop == false){
					switch(result){
						case DUCO_MESSAGE_ROW_END: {
                  			receivedNewValue = Plugin_196_readFanSpeedProcessRow(PLUGIN_LOG_PREFIX_196, event->BaseVarIndex, PCONFIG(P196_CONFIG_LOG_SERIAL));
							if(receivedNewValue) sendData(event);
							duco_serial_bytes_read = 0; // reset bytes read counter
							break;
						}
						case DUCO_MESSAGE_END: {
				     		DucoThrowErrorMessage(PLUGIN_LOG_PREFIX_196, result);
							DucoTaskStopSerial(PLUGIN_LOG_PREFIX_196);
							serialPortInUseByTask = 255;
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
					DucoSerialSendCommand(PLUGIN_LOG_PREFIX_196);
				}
			}
	   		success = true;
    		break;
  		}

	}
  	return success;
}

void Plugin_196_startReadFanSpeed(String logPrefix){
	// set this variables before sending command
	ducoSerialStartReading = millis();
	duco_serial_bytes_read = 0; // reset bytes read counter
	duco_serial_rowCounter = 0; // reset row counter

   	// SEND COMMAND
	char command[] = "fanspeed\r\n";
	DucoSerialStartSendCommand(command);
}


	/* FanSpeed
		.fanspeed
		FanSpeed: Actual 389 [rpm] - Filtered 391 [rpm]
		Done

		>
	*/
bool Plugin_196_readFanSpeedProcessRow(String logPrefix, uint8_t userVarIndex, bool serialLoggingEnabled){
 
	String log;
	if(serialLoggingEnabled && loglevelActiveFor(LOG_LEVEL_DEBUG)){	     
		log = logPrefix;
		log += F("ROW: ");
		log += duco_serial_rowCounter;
		log += F(" bytes read:");
		log += duco_serial_bytes_read;
		addLogMove(LOG_LEVEL_DEBUG, log);     
		DucoSerialLogArray(logPrefix, duco_serial_buf, duco_serial_bytes_read, 0);
	}


	// get the first row to check for command
	if(duco_serial_rowCounter == 1){
		char command[] = "fanspeed";
		if (DucoSerialCheckCommandInResponse(logPrefix, command) ) {
			if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
				log = logPrefix;
				log += F("Received correct response on fanspeed");
				addLogMove(LOG_LEVEL_DEBUG, log);
			}
		} else {
			if(loglevelActiveFor(LOG_LEVEL_DEBUG)){
				log = logPrefix;
				log += F("Received invalid response on fanspeed");
				addLogMove(LOG_LEVEL_DEBUG, log);
			}
			DucoTaskStopSerial(logPrefix);
			return false;
		}

	}else if( duco_serial_rowCounter > 1){
		duco_serial_buf[duco_serial_bytes_read] = '\0';
		unsigned int fanSpeedFiltered = 0;

		if (sscanf((const char*)duco_serial_buf, "  FanSpeed: Actual %*u %*s %*s %*s %u", &fanSpeedFiltered) == 1) {
			UserVar[userVarIndex] = fanSpeedFiltered;           
			
			char logBuf[55];
			snprintf(logBuf, sizeof(logBuf), "Fanspeed: %u RPM", fanSpeedFiltered);
         	log = logPrefix;
			log += logBuf;
         	addLogMove(LOG_LEVEL_INFO, log);
			return true;		
		} 

	}
	return false;
} 