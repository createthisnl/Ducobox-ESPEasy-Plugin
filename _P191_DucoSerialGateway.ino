//#################################### Plugin 191: DUCO Serial Gateway ##################################
//
//  DUCO Serial Gateway to read Ducobox data
//  https://github.com/arnemauer/Ducobox-ESPEasy-Plugin
//  http://arnemauer.nl/ducobox-gateway/
//#######################################################################################################

#include "_Plugin_Helper.h"
#include "DucoSerialHelpers.h"

#define PLUGIN_191
#define PLUGIN_ID_191 191
#define PLUGIN_NAME_191 "DUCO Serial Gateway"
#define PLUGIN_VALUENAME1_191 "DUCO_STATUS" // networkcommand
#define PLUGIN_VALUENAME2_191 "FLOW_PERCENTAGE"     // networkcommand
#define PLUGIN_READ_TIMEOUT_191 3000
#define PLUGIN_LOG_PREFIX_191 String("[P191] DUCO SER GW: ")
#define DUCOBOX_NODE_NUMBER 1
#define PLUGIN_191_HARDWARE_83_AND_LOWER_SERIALSWITCH_PIN 2
#define PLUGIN_191_PCF_ADDRESS 0x38 // 7-bit address!

boolean Plugin_191_init = false;
boolean ventilation_gateway_disable_serial = false;
boolean ventilation_gateway_serial_status_led = false; // false = off, true=on; need this to prevent continues turning gpio off (or sending command to PCF) in PLUGIN_TEN_PER_SECOND

// 0 -> "auto" = AutomaticMode;
// 1 -> "man1" = ManualMode1;
// 2 -> "man2" = ManualMode2;
// 3 -> "man3" = ManualMode3;
// 4 -> "empt" = EmptyHouse;
// 5 -> "alrm" = AlarmMode;
// 11 -> "cnt1" = PermanentManualMode1;
// 12 -> "cnt2" = PermanentManualMode2;
// 13 -> "cnt3"= PermanentManualMode3;
// 20 -> "aut0" = AutomaticMode;
// 21 -> "aut1" = Boost10min;
// 22 -> "aut2" = Boost20min;
// 23 -> "aut3" = Boost30min;

#define DUCO_STATUS_MODUS_COUNT 13
const char DucoStatusModes[DUCO_STATUS_MODUS_COUNT][5] =
    {"AUTO", "MAN1", "MAN2", "MAN3", "EMPT", "ALRM", "CNT1", "CNT2", "CNT3", "AUT0", "AUT1", "AUT2", "AUT3"};
const int DucoStatusModesNumbers[DUCO_STATUS_MODUS_COUNT] =
    {0, 1, 2, 3, 4, 5, 11, 12, 13, 20, 21, 22, 23};

typedef enum {
	P191_CONFIG_VALUE_TYPE = 0,
	P191_CONFIG_LOG_SERIAL = 1,
	P191_CONFIG_USE_FOR_NETWORK_TOOL = 2,
	P191_CONFIG_HARDWARE_TYPE = 3,
} P191PluginConfigs; // max 8 values

typedef enum {
	P191_VALUE_VENTMODE = 0,
	P191_VALUE_VENT_PERCENTAGE = 1,
	P191_VALUE_CURRENT_FAN = 2,
	P191_VALUE_COUNTDOWN = 3,
	P191_VALUE_NONE = 4,
} P191Values;

typedef enum {
	P191_HARDWARE_DIY = 0,
	P191_HARDWARE_83_AND_LOWER = 1,
	P191_HARDWARE_84_AND_HIGHER = 2,
} P191HardwareTypes;

//global variables
int P191_DATA_VALUE[4]; // store here data of P191_VALUE_VENTMODE, P191_VALUE_VENT_PERCENTAGE, P191_VALUE_CURRENT_FAN and P191_VALUE_COUNTDOWN
bool P191_mainPluginActivatedInTask = false;
bool P191_disableCoreLogLevel = true; // if this variable is true de plugin is trying to 'disable' coreloglevel to prevent debug output from the Ducobox on the serial line (sometimes Duco network tool doesnt disable this).

// when calling 'PLUGIN_READ', if serial port is in use set this flag and check in PLUGIN_ONCE_A_SECOND if serial port is free.
bool P191_waitingForSerialPort = false;

#define P191_NR_OUTPUT_VALUES 4 // how many values do we need to read from networklist.
uint8_t varColumnNumbers[P191_NR_OUTPUT_VALUES]; // stores the columnNumbers

uint8_t P191_PCF_pin_state = 0b11110111; // pin P0 needs to be high (input)


#define P191_VALUENAME_NR_OUTPUT_OPTIONS 5
String Plugin_191_valuename(byte value_nr, bool displayString) {
	switch (value_nr) {
		case P191_VALUE_VENTMODE:  return displayString ? F("Ventilationmode") : F("stat");
		case P191_VALUE_VENT_PERCENTAGE:  return displayString ? F("Ventilation_Percentage") : F("%dbt");
		case P191_VALUE_CURRENT_FAN:  return displayString ? F("Current_Fan_Value") : F("cval");
		case P191_VALUE_COUNTDOWN:  return displayString ? F("Countdown") : F("cntdwn");
		case P191_VALUE_NONE: return displayString ? F("None") : F("");
		default:
      break;
  	}
  	return "";
}

#define P191_HARDWARE_NR_OUTPUT_OPTIONS 3
String Plugin_191_hardware_type(byte value_nr) {
	switch (value_nr) {
		case P191_HARDWARE_DIY:  return F("DIY esp8266 hardware");
		case P191_HARDWARE_83_AND_LOWER:  return F("Ventilation gateway V8.3 and lower");
		case P191_HARDWARE_84_AND_HIGHER: return F("Ventilation gateway V8.4 and higher");
		default:
      break;
  	}
  	return "";
}



boolean Plugin_191(byte function, struct EventStruct *event, String &string){
	boolean success = false;
	switch (function){

		case PLUGIN_DEVICE_ADD:{
			Device[++deviceCount].Number = PLUGIN_ID_191;
			Device[deviceCount].Type = DEVICE_TYPE_SINGLE;
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

		case PLUGIN_GET_DEVICENAME: {
		string = F(PLUGIN_NAME_191);
			break;
		}

		case PLUGIN_GET_DEVICEVALUENAMES: {
			safe_strncpy(ExtraTaskSettings.TaskDeviceValueNames[0], Plugin_191_valuename(PCONFIG(P191_CONFIG_VALUE_TYPE), true), sizeof(ExtraTaskSettings.TaskDeviceValueNames[0]));
			break;
		}

		case PLUGIN_GET_DEVICEGPIONAMES:{
			event->String1 = formatGpioName_output(F("Status LED"));
			event->String2 = formatGpioName_output(F("Use as Network Tool"));
			break;
		}

		case PLUGIN_EXIT: {
			if(PCONFIG(P191_CONFIG_VALUE_TYPE) == P191_VALUE_VENTMODE){
				P191_mainPluginActivatedInTask = false;
				if(serialPortInUseByTask == event->TaskIndex){
					serialPortInUseByTask = 255;
				}


				if(PCONFIG(P191_CONFIG_HARDWARE_TYPE) == P191_HARDWARE_84_AND_HIGHER){
					P191_PCF_set_pin_output(0, HIGH); // PCF8574; P0 serial_switch_button = HIGH (input);
					P191_PCF_set_pin_output(1, HIGH); // PCF8574; P1 SER_SWITCH_CONNECT_DUCO =  HIGH  (disconnect uart ducobox)
					P191_PCF_set_pin_output(2, HIGH); // PCF8574; P2 SER_SWITCH_SWAP_UART = HIGH (uart NOT swapped)
					P191_PCF_set_pin_output(4, HIGH); // PCF8574; P4 LED_SERIAL_SWITCH_GREEN =  HIGH (led off)
					P191_PCF_set_pin_output(5, HIGH); // PCF8574; P5 LED_GREEN 	= HIGH (Led off)
					//P191_PCF_set_pin_output(6, HIGH); // PCF8574; P6 LED_YELLOW	= HIGH (Led off)
					//P191_PCF_set_pin_output(7, HIGH); // PCF8574; P7 LED_BLUE		= HIGH (Led off)
				}
			}
			clearPluginTaskData(event->TaskIndex); // clear plugin taskdata
			addLogMove(LOG_LEVEL_INFO, PLUGIN_LOG_PREFIX_191 + F("EXIT PLUGIN_191"));	
			success = true;
			break;
		}

		case PLUGIN_WEBFORM_LOAD: {

			addRowLabel(F("Output Value Type"));
			addSelector_Head(PCONFIG_LABEL(P191_CONFIG_VALUE_TYPE));

			for (byte x = 0; x < P191_VALUENAME_NR_OUTPUT_OPTIONS; x++){
				String name     = Plugin_191_valuename(x, true);
				bool   disabled = false;

				if(x == 0){
					name = F("Ventilationmode (main plugin)");
					if(P191_mainPluginActivatedInTask && (PCONFIG(P191_CONFIG_VALUE_TYPE) != P191_VALUE_VENTMODE) ){
						disabled = true;
					}
				}else{
					if(P191_mainPluginActivatedInTask == false || (PCONFIG(P191_CONFIG_VALUE_TYPE) == P191_VALUE_VENTMODE) ){
						disabled = true;
					}
				}

				addSelector_Item(name, x, PCONFIG(P191_CONFIG_VALUE_TYPE) == x, disabled, "");
			}
			addSelector_Foot();

			if(PCONFIG(P191_CONFIG_VALUE_TYPE) == P191_VALUE_VENTMODE){

				addRowLabel(F("Hardware type"));
				addSelector_Head_reloadOnChange(PCONFIG_LABEL(P191_CONFIG_HARDWARE_TYPE), F("wide"), false);

				for (byte x = 0; x < P191_HARDWARE_NR_OUTPUT_OPTIONS; x++){
					addSelector_Item(Plugin_191_hardware_type(x), x, PCONFIG(P191_CONFIG_HARDWARE_TYPE) == x, false, "");
				}
				addSelector_Foot();

				addFormCheckBox(F("Log serial messages to syslog"), F("Plugin151_log_serial"), PCONFIG(P191_CONFIG_LOG_SERIAL));

				// if hardware is ventilation gateway show option to use as usb-cable
				if(PCONFIG(P191_CONFIG_HARDWARE_TYPE) != P191_HARDWARE_DIY){
					addFormCheckBox(F("Disable gateway serial to use gateway as usb-cable for network tool"), F("Plugin151_use_for_network_tool"), PCONFIG(P191_CONFIG_USE_FOR_NETWORK_TOOL));
				}
			}

			success = true;
			break;
		}

		case PLUGIN_SET_DEFAULTS:{
			PCONFIG(P191_CONFIG_VALUE_TYPE) = P191_VALUE_NONE;
			success = true;
			break;
		}

		case PLUGIN_WEBFORM_SAVE:{

			int newValueType = getFormItemInt(PCONFIG_LABEL(P191_CONFIG_VALUE_TYPE), 0);

			// if value_type has changed, empty value name so default value name will be used for TaskDeviceValueNames[0]
			if (newValueType != PCONFIG(P191_CONFIG_VALUE_TYPE)) {
				ZERO_FILL(ExtraTaskSettings.TaskDeviceValueNames[0]);
			}

			PCONFIG(P191_CONFIG_VALUE_TYPE) = newValueType;

			if(PCONFIG(P191_CONFIG_VALUE_TYPE) == P191_VALUE_VENTMODE){
				P191_mainPluginActivatedInTask = true;

				PCONFIG(P191_CONFIG_HARDWARE_TYPE) = getFormItemInt(PCONFIG_LABEL(P191_CONFIG_HARDWARE_TYPE), 0);
				PCONFIG(P191_CONFIG_LOG_SERIAL) = isFormItemChecked(F("Plugin151_log_serial")); 

				// if hardware is ventilation gateway show option to use as usb-cable
				if(PCONFIG(P191_CONFIG_HARDWARE_TYPE) != P191_HARDWARE_DIY){
					PCONFIG(P191_CONFIG_USE_FOR_NETWORK_TOOL) = isFormItemChecked(F("Plugin151_use_for_network_tool")); 
				}else{
					PCONFIG(P191_CONFIG_USE_FOR_NETWORK_TOOL) = false; // if hardware is diy, set false.
				}

				if(PCONFIG(P191_CONFIG_HARDWARE_TYPE) == P191_HARDWARE_84_AND_HIGHER){
					Settings.TaskDevicePin1[event->TaskIndex] = -1;
				}
			}

			success = true;
			break;
		}

		case PLUGIN_INIT: {
			
			// load tasksettings, if we dont call this function, PCONFIG (Settings.TaskDevicePluginConfig) will be 0.
			//LoadTaskSettings(event->TaskIndex);

			// only initiate serial if we are the task with main plugin activated.
			if(PCONFIG(P191_CONFIG_VALUE_TYPE) == P191_VALUE_VENTMODE){
				P191_mainPluginActivatedInTask = true;

				// set status led
				if (CONFIG_PIN1 != -1 && PCONFIG(P191_CONFIG_HARDWARE_TYPE) != P191_HARDWARE_84_AND_HIGHER){
					pinMode(CONFIG_PIN1, OUTPUT);
					digitalWrite(CONFIG_PIN1, HIGH);
				}

				// set a flag voor plugins P190, P191, P192, P193, P194, P195, P196 to stop using serial
				ventilation_gateway_disable_serial = PCONFIG(P191_CONFIG_USE_FOR_NETWORK_TOOL);

				// if hardwaretype is ventilation gateway show option to use as usb-cable
				if(PCONFIG(P191_CONFIG_HARDWARE_TYPE) == P191_HARDWARE_83_AND_LOWER){
					// set pin to control TS3A5017DRD16-M high
					pinMode(PLUGIN_191_HARDWARE_83_AND_LOWER_SERIALSWITCH_PIN, OUTPUT); // gpio2 = D4

					if(PCONFIG(P191_CONFIG_USE_FOR_NETWORK_TOOL)) {
						Serial.end();  // stop serial
						//put pins in input mode (High impedance) to prevent interference
						pinMode(1, INPUT); // TX
						pinMode(3, INPUT); // RX0
						digitalWrite(PLUGIN_191_HARDWARE_83_AND_LOWER_SERIALSWITCH_PIN, LOW); // TS3A5017DRD16-M > LOW = TX/RX of PC usb-serial connected to ducobox
					}else{
						digitalWrite(PLUGIN_191_HARDWARE_83_AND_LOWER_SERIALSWITCH_PIN, HIGH); // TS3A5017DRD16-M > high =  PC usb-serial connected to gateway
						// reset pinmode of RX and TX to default
						pinMode(1, FUNCTION_0); // TX
						pinMode(3, FUNCTION_0); // RX0
						Serial.begin(115200, SERIAL_8N1);
					}
				}else if(PCONFIG(P191_CONFIG_HARDWARE_TYPE) == P191_HARDWARE_84_AND_HIGHER){
					String log = PLUGIN_LOG_PREFIX_191;
					log += F("P191_HARDWARE_84_AND_HIGHER");
					addLogMove(LOG_LEVEL_DEBUG, log);
					Wire.begin();

					// >>>>> SET PCF8574 SET pin P1 HIGH = all switches are high impedance
					// PCF8574 SET P1 LOW	
					//P191_PCF_pin_state = 0xF7; // 0xF7 = 0b1111.0111; P0 serial_switch_button = HIGH (input); P1 SER_SWITCH_CONNECT_DUCO = HIGH (default duco uart not connected); P2 SER_SWITCH_SWAP_UART = HIGH (uart NOT swapped)
					P191_PCF_set_pin_output(0, HIGH); // PCF8574; P0 serial_switch_button = HIGH (input);
					P191_PCF_set_pin_output(1, HIGH); // PCF8574; P1 SER_SWITCH_CONNECT_DUCO =  HIGH  (disconnect uart ducobox)
					P191_PCF_set_pin_output(2, HIGH); // PCF8574; P2 SER_SWITCH_SWAP_UART = HIGH (uart NOT swapped)
					P191_PCF_set_pin_output(4, HIGH); // PCF8574; P4 LED_SERIAL_SWITCH_GREEN =  HIGH (led off)
					P191_PCF_set_pin_output(5, HIGH); // PCF8574; P5 LED_GREEN 	= HIGH (Led off)

					
					// if checkbox "use for network tool" is checked, Serial tx/rx of ducobox is swapped with tx/rx of usb converter
					// We need to disable the RX/TX pin to prevent interference between the cp2104 and ducobox.
					if(PCONFIG(P191_CONFIG_USE_FOR_NETWORK_TOOL)) {
						addLogMove(LOG_LEVEL_DEBUG, PLUGIN_LOG_PREFIX_191 + F("networktool active"));

						Serial.end();  // stop serial
						//put pins in input mode (High impedance) to prevent interference
						pinMode(1, INPUT); // TX
						pinMode(3, INPUT); // RX0
						
						P191_PCF_set_pin_output(1, LOW); // PCF8574 SET P1 SER_SWITCH_CONNECT_DUCO =  LOW  (connect uart ducobox)
						P191_PCF_set_pin_output(2, LOW); // PCF8574 SET P2 SER_SWITCH_SWAP_UART LOW  (uart swapped)
						P191_PCF_set_pin_output(4, LOW); // PCF8574 SET P4 LED_SERIAL_SWITCH_GREEN = DOWN (led on)

					}else{
						// >>>>> SET PCF8574 SET pin P2 HIGH = normal LOW = swap TX/RX
						// PCF8574 SET P2 LOW
						P191_PCF_set_pin_output(1, LOW); // PCF8574 SET P1 SER_SWITCH_CONNECT_DUCO =  LOW  (connect uart ducobox)
						P191_PCF_set_pin_output(2, HIGH); // PCF8574 SET P2 SER_SWITCH_SWAP_UART HIGH  (uart NOT swapped)

						// reset pinmode of RX and TX to default
						pinMode(1, FUNCTION_0); // TX
						pinMode(3, FUNCTION_0); // RX0
						Serial.begin(115200, SERIAL_8N1);
					}	


				}else if(PCONFIG(P191_CONFIG_HARDWARE_TYPE) == P191_HARDWARE_DIY){
					// reset pinmode of RX and TX to default
					pinMode(1, FUNCTION_0); // TX
					pinMode(3, FUNCTION_0); // RX0
					Serial.begin(115200, SERIAL_8N1);
				}

				Plugin_191_init = true;
				// disable coreloglevel on ducobox
				P191_disableCoreLogLevel = true;
			}
			
			success = true;
			break;
	}


		case PLUGIN_I2C_HAS_ADDRESS:{
			if(PCONFIG(P191_CONFIG_HARDWARE_TYPE) == P191_HARDWARE_84_AND_HIGHER){
				success = true;
			}
			break;
		}


		case PLUGIN_ONCE_A_SECOND:{
			String log;

			if (Plugin_191_init && PCONFIG(P191_CONFIG_VALUE_TYPE) == P191_VALUE_VENTMODE ){
				if(P191_waitingForSerialPort){
					if(serialPortInUseByTask == 255){
						Plugin_191(PLUGIN_READ, event, string);
						P191_waitingForSerialPort = false;
					}
				}

				if(serialPortInUseByTask == event->TaskIndex){
					if( (millis() - ducoSerialStartReading) > PLUGIN_READ_TIMEOUT_191){
						log = PLUGIN_LOG_PREFIX_191;
						log += F("Serial reading timeout");
						addLogMove(LOG_LEVEL_DEBUG, log);

						DucoTaskStopSerial(PLUGIN_LOG_PREFIX_191);
						serialPortInUseByTask = 255;
					}
				}
			}

		if(UserVar[event->BaseVarIndex] != P191_DATA_VALUE[PCONFIG(P191_CONFIG_VALUE_TYPE)] ){
			UserVar[event->BaseVarIndex]  = P191_DATA_VALUE[PCONFIG(P191_CONFIG_VALUE_TYPE)];
				sendData(event); // force to send data to controller
				log = PLUGIN_LOG_PREFIX_191;
				log += F("New value for ");
				log += Plugin_191_valuename( PCONFIG(P191_CONFIG_VALUE_TYPE), true);
				addLogMove(LOG_LEVEL_DEBUG, log);
			}

			success = true;
			break;
		}

		case PLUGIN_SERIAL_IN: {
			// if we unexpectedly receive data we need to flush and return success=true so espeasy won't interpret it as an serial command.
			// also ignore data if serial is disabled 
			if(serialPortInUseByTask == 255 || ventilation_gateway_disable_serial){
				DucoSerialFlush();
				success = true;
			}

			if(serialPortInUseByTask == event->TaskIndex){
				uint8_t result = 0;
				bool stop = false;
				
				while(result != DUCO_MESSAGE_FIFO_EMPTY && stop == false){
					result = DucoSerialInterrupt();
						
					switch(result){
						case DUCO_MESSAGE_ROW_END: {
							if(P191_disableCoreLogLevel){
								Plugin_191_disableCoreLogLevel_processRow(event,  PCONFIG(P191_CONFIG_LOG_SERIAL));
							}else{
								Plugin_191_processRow(event,  PCONFIG(P191_CONFIG_LOG_SERIAL));
							}
							duco_serial_bytes_read = 0; // reset bytes read counter
							break;
						}
						case DUCO_MESSAGE_END: {
							DucoThrowErrorMessage(PLUGIN_LOG_PREFIX_191, result);
							DucoTaskStopSerial(PLUGIN_LOG_PREFIX_191);
							stop = true;
							break;
						}
					}
				}
				success = true;
			}

			break;
		}

		case PLUGIN_READ:{

			if(Plugin_191_init && PCONFIG(P191_CONFIG_VALUE_TYPE) == P191_VALUE_VENTMODE && ventilation_gateway_disable_serial == false){
				// check if serial port is in use by another task, otherwise set flag.
				if(serialPortInUseByTask == 255){
					serialPortInUseByTask = event->TaskIndex;
					
					if(P191_disableCoreLogLevel){
						Plugin_191_disableCoreLogLevel();
					}else{
						if (PCONFIG(P191_CONFIG_HARDWARE_TYPE) == P191_HARDWARE_84_AND_HIGHER){ // status led
							P191_PCF_set_pin_output(5, LOW); // PCF8574 SET P5 LED_GREEN = LOW (led on)
							ventilation_gateway_serial_status_led = true;
						}else if (CONFIG_PIN1 != -1 ){
							digitalWrite(CONFIG_PIN1, LOW);
							ventilation_gateway_serial_status_led = true;
						}

						Plugin_191_startReadNetworkList();
					}

				}else{
					String log;
					log = PLUGIN_LOG_PREFIX_191;
					log += F("Serial port in use, set flag to read data later.");
					addLogMove(LOG_LEVEL_DEBUG, log);
					P191_waitingForSerialPort = true;
				}
			}
			success = true;
			break;
		}

		case PLUGIN_TEN_PER_SECOND: {
			if(Plugin_191_init && ventilation_gateway_serial_status_led ){
				if (PCONFIG(P191_CONFIG_HARDWARE_TYPE) == P191_HARDWARE_84_AND_HIGHER){ // status led off
					P191_PCF_set_pin_output(5, HIGH); // PCF8574 SET P5 LED_GREEN = HIGH (led off)
				}else if (CONFIG_PIN1 != -1 ){
					digitalWrite(CONFIG_PIN1, HIGH);
				}
				ventilation_gateway_serial_status_led = false;

			}
		success = true;
			break;
		}


		case PLUGIN_FIFTY_PER_SECOND: {
			if(Plugin_191_init){
				if(serialPortInUseByTask == event->TaskIndex){
					if(serialSendCommandInProgress){
						DucoSerialSendCommand(PLUGIN_LOG_PREFIX_191);
					}
				}
			}
			success = true;
			break;
		}



	} // switch() end
	return success;
}


/* Example output:
  coreloglevel 0x10
  Done
  */
void Plugin_191_disableCoreLogLevel(){
	String log;
	log = PLUGIN_LOG_PREFIX_191;
	log += F("Start disableCoreLogLevel");
	addLogMove(LOG_LEVEL_DEBUG, log);
	P191_disableCoreLogLevel = true;
	// set this variables before sending command
	ducoSerialStartReading = millis();
	duco_serial_bytes_read = 0; // reset bytes read counter
	duco_serial_rowCounter = 0; // reset row counter

	// SEND COMMAND
	DucoSerialStartSendCommand("coreloglevel 0x10\r\n");
}


void Plugin_191_disableCoreLogLevel_processRow(struct EventStruct *event, bool serialLoggingEnabled ){
    
	if ( PCONFIG(P191_CONFIG_LOG_SERIAL)){
		if(duco_serial_rowCounter <4 ){
			String prefix;
			prefix += PLUGIN_LOG_PREFIX_191;
			prefix += F("ROW: ");
			prefix += duco_serial_rowCounter;
			prefix += F(" bytes read:");
			prefix += duco_serial_bytes_read;
			DucoSerialLogArray(prefix, duco_serial_buf, duco_serial_bytes_read, 0);
		}
	}

	String log;
	if(duco_serial_rowCounter == 1){
		if (DucoSerialCheckCommandInResponse(PLUGIN_LOG_PREFIX_191, "coreloglevel 0x10") ) {
			log = PLUGIN_LOG_PREFIX_191;
			log += F("Received correct response on coreloglevel 0x10");
     		addLogMove(LOG_LEVEL_DEBUG, log);
      } else {
		  	log = PLUGIN_LOG_PREFIX_191;
			log += F("Received invalid response on coreloglevel 0x10");
     		addLogMove(LOG_LEVEL_DEBUG, log);
			DucoTaskStopSerial(PLUGIN_LOG_PREFIX_191);
			P191_disableCoreLogLevel = false;
         return;
      }
	}else if( duco_serial_rowCounter == 2){
		if(DucoSerialCheckCommandInResponse(PLUGIN_LOG_PREFIX_191, "  Done")){ // re-use this function to find "Done" in the response;
			log = PLUGIN_LOG_PREFIX_191;
			log += F("coreloglevel 0x10 is set on Ducobox");
     		addLogMove(LOG_LEVEL_DEBUG, log);
    	} else {
			log = PLUGIN_LOG_PREFIX_191;
			log += F("Ducobox did not confirm coreloglevel change.");
     		addLogMove(LOG_LEVEL_DEBUG, log);
	  	}
		DucoTaskStopSerial(PLUGIN_LOG_PREFIX_191);
		P191_disableCoreLogLevel = false;
		return;
	}
}

void Plugin_191_startReadNetworkList(){
	String log;
	log = PLUGIN_LOG_PREFIX_191;
	log += F("Start readNetworkList");
	addLogMove(LOG_LEVEL_DEBUG, log);
	
	// set this variables before sending command
	ducoSerialStartReading = millis();
	duco_serial_bytes_read = 0; // reset bytes read counter
	duco_serial_rowCounter = 0; // reset row counter

	// SEND COMMAND
	DucoSerialStartSendCommand("Network\r\n");
}


void Plugin_191_processRow(struct EventStruct *event, bool serialLoggingEnabled ){
    
	if ( PCONFIG(P191_CONFIG_LOG_SERIAL)){
		if(duco_serial_rowCounter <3 ){
			String prefix;
			prefix += PLUGIN_LOG_PREFIX_191;
			prefix += F("ROW: ");
			prefix += duco_serial_rowCounter;
			prefix += F(" bytes read:");
			prefix += duco_serial_bytes_read;
			DucoSerialLogArray( prefix, duco_serial_buf, duco_serial_bytes_read, 0);
		}
	}

	String log;
	if(duco_serial_rowCounter == 1){
		if (DucoSerialCheckCommandInResponse(PLUGIN_LOG_PREFIX_191, "network") ) { // gw send command "Network" with capital N, older ducoboxes returns "network" without capital N
			log = PLUGIN_LOG_PREFIX_191;
			log += F("Received correct response on network");
     		addLogMove(LOG_LEVEL_DEBUG, log);
      	} else if (DucoSerialCheckCommandInResponse(PLUGIN_LOG_PREFIX_191, "Network") ) { // for newer ducobox e.g. Ducobox Energy comfort 325R
			log = PLUGIN_LOG_PREFIX_191;
			log += F("Received correct response on network");
     		addLogMove(LOG_LEVEL_DEBUG, log);
      	} else {
		  	log = PLUGIN_LOG_PREFIX_191;
			log += F("Received invalid response on network");
     		addLogMove(LOG_LEVEL_DEBUG, log);
			DucoTaskStopSerial(PLUGIN_LOG_PREFIX_191);
         	return;
      	}

	}else if( duco_serial_rowCounter == 4){
		// ROW 4: columnnames => get columnnumbers by columnname
		Plugin_191_getColumnNumbersByColumnName(event, varColumnNumbers);

	}else if( duco_serial_rowCounter > 4){
		duco_serial_buf[duco_serial_bytes_read] = '\0'; // null terminate string
    	int splitCounter = 0;
    	char *buf = (char *)duco_serial_buf;
    	char *token = strtok(buf, "|");

      	if (token != NULL){
      		long nodeAddress = strtol(token, NULL, 0);
			if (nodeAddress == DUCOBOX_NODE_NUMBER) {
				log = PLUGIN_LOG_PREFIX_191;
				log += F("Ducobox row found!");
				addLogMove(LOG_LEVEL_DEBUG, log);
				while (token != NULL) {
						// check if there is a match for varColumnNumbers 
						for (int i = 0; i < P191_NR_OUTPUT_VALUES; i++){
							if (splitCounter == varColumnNumbers[i]) {
								int newValue = -1;

								if(i == P191_VALUE_VENTMODE){  // ventilationmode / "stat"
									newValue = Plugin_191_parseVentilationStatus(token);			
								}else if(i < P191_NR_OUTPUT_VALUES){ // parse numeric value:"cntdwn", "%dbt", "trgt", "cval", "snsr", "ovrl"
									newValue = Plugin_191_parseNumericValue(token);			
								}

								if(newValue != -1){
									P191_DATA_VALUE[i] = newValue;
									char logBuf[20];
									snprintf(logBuf, sizeof(logBuf), " value: %d", newValue);
									log = PLUGIN_LOG_PREFIX_191;
									log += F("New ");
									log += Plugin_191_valuename(i,true);
									log += logBuf;
									addLogMove(LOG_LEVEL_DEBUG, log);
								}
							}
						} // for end

					splitCounter++;
					token = strtok(NULL, "|");
				} //while (token != NULL)
			}
    	}
	}
	return;
} // end of Plugin_191_processRow()

void Plugin_191_getColumnNumbersByColumnName(struct EventStruct *event, uint8_t varColumnNumbers[]) {

	for (int i = 0; i < P191_NR_OUTPUT_VALUES; i++) {
		varColumnNumbers[i] = 255; // set a default value in case we cant find a column
	}

	duco_serial_buf[duco_serial_bytes_read] = '\0';
  	int splitCounter = 0;
  	char *buf = (char *)duco_serial_buf;
  	char *token = strtok(buf, "|");

	while (token != NULL) {
		// check if there is a match with
    	for (int i = 0; i < P191_NR_OUTPUT_VALUES; i++) {
			if (strstr(token, Plugin_191_valuename(i, false).c_str() ) != NULL){ // if not null, save position
        		varColumnNumbers[i] = splitCounter;
      		}
		} // for
		splitCounter++;
		token = strtok(NULL, "|");

	} //while (token != NULL) {

  	return;
}

int Plugin_191_parseVentilationStatus(char *token){
  	for (int i = 0; i < DUCO_STATUS_MODUS_COUNT; i++) {
    	// strstr is case sensitive!
    	if (strstr(token, DucoStatusModes[i]) != NULL) { // if not null, status found :)
			return DucoStatusModesNumbers[i];
    	}
  	}

	// if value not found return -1
	String log;
	log = PLUGIN_LOG_PREFIX_191;
	log += F("Status value not found.");
	addLogMove(LOG_LEVEL_DEBUG, log);
	return -1;
}

int Plugin_191_parseNumericValue(char *token) {
	int raw_value;
  	if (sscanf(token, "%d", &raw_value) == 1){
   		return raw_value; /* No conversion required */
  	}else{
		String log;
		log = PLUGIN_LOG_PREFIX_191;
		log += F("Numeric value not found.");
		addLogMove(LOG_LEVEL_DEBUG, log);
  		return -1;
	}

}

// port P0 - P7
void P191_PCF_set_pin_output(uint8_t port, bool state){
	if(state){ // set bit high
		P191_PCF_pin_state =	(P191_PCF_pin_state | (1 << port));
	}else{
		P191_PCF_pin_state = (P191_PCF_pin_state & (~(1 << port))); // set bit low
	}

	// send outputs to PCF
	Plugin_191_PCF8574_setReg(PLUGIN_191_PCF_ADDRESS, P191_PCF_pin_state);

}


void Plugin_191_PCF8574_setReg(uint8_t addr, uint8_t data){
//	char logBuf[30];
//	snprintf(logBuf, sizeof(logBuf), "addr %#x value %#x", addr, data);
//	addLogMove(LOG_LEVEL_DEBUG, PLUGIN_LOG_PREFIX_191 + "PCF DATA " + logBuf);
						
  Wire.beginTransmission(addr);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t Plugin_191_PCF8574_getReg(uint8_t addr){
	Wire.requestFrom(addr, (uint8_t)0x1);

	if (Wire.available()){
		return Wire.read();
	}
	return 0xFF;
}
