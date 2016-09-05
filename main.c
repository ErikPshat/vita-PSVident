#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/power.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/system_param.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/screenshot.h>
#include <psp2/sysmodule.h>

#include "graphics.h"

#define printf psvDebugScreenPrintf
#define NET_INIT_SIZE 1 * 1024 * 1024
#define NET_CTL_ERROR_NOT_TERMINATED 0x80412102


/* TO DO
- decrypt & read index.dat for real firmware (0x03600011)
- fix HENkaku font string format
- get region_no with registry functions??
- add vshMemoryCardGetCardInsertState check & more error handling
- screenshot
- clean up
*/


/* Changelog
v0.26
- removed GPU clock
- added advanced Battery Info (disabled for PSTV now)

v0.25
- adjusted the font spacing for better readability
- fixed Device detection for HENkaku v4
- removed Hardware info until we know what they mean

v0.2
- added update values option
- added IDPS
- added Hardware info
- added CEX/DEX/TEST/TOOL & Mode detection
- code cleaning
*/

//! for MAC
static SceNetEtherAddr mac;
static char mac_string[32];
static void *net_memory = NULL;

//! id.dat
char buff[255];
char mid[50];  //unknown
char dig[50];  //unknown
char did[50];  //PSID
char aid[50];  //DRM Account name - or "NP/account_id" in registry
char oid[255]; //username
char svr[50];  //firmware

//! Registry
int sceRegMgrGetKeyInt(const char* reg, const char* key, int* val);
int sceRegMgrGetKeyStr(const char* reg, const char* key, char* str, const int buf_size);

//! Battery
int scePowerIsBatteryExist();
int scePowerGetBatteryTemp();
int scePowerGetBatteryVolt();
int scePowerGetBatterySOH();

//! Vita model
int sceKernelGetModelForCDialog();

//! System Version
typedef struct {
	SceUInt size;
	SceChar8 version_string[28];
	SceUInt version_value;
	SceUInt unk;
} SceSystemSwVersionParam;
int sceKernelGetSystemSwVersion(SceSystemSwVersionParam *param);


//! Console CID/IDPS
int _vshSblAimgrGetConsoleId(char CID[16]);

char *getCID() {
	
	int i;
	char CID[16];
	static char cid_string[16];
	
	_vshSblAimgrGetConsoleId(CID);

	for (i = 0; i < 16; i++) {
		sprintf(cid_string + strlen(cid_string), "%02X", CID[i]);
	}
	
	return cid_string;
}

//! clock freq
int getClockFrequency(int no){
	if (no == 0) return scePowerGetArmClockFrequency();
	else if (no == 1)	return scePowerGetBusClockFrequency();
	else if (no == 2)	return scePowerGetGpuClockFrequency();
	else return 0;
}
	
//! CEX, DEX, Test, IDU
int vshSblAimgrIsCEX();				//retail
int vshSblAimgrIsDEX();
int vshSblAimgrIsDolce();			//PSTV
int vshSblAimgrIsGenuineDolce();	//PSTV
int vshSblAimgrIsGenuineVITA();		//Vita Fat&Slim
int vshSblAimgrIsTest();	
int vshSblAimgrIsTool();
int vshSblAimgrIsVITA();			//Vita Fat&Slim
int vshSysconIsIduMode();			//is IDU device (not is in DEMO MODE currently!)
int vshSysconIsShowMode();			//is in Show Mode
	
const char* mode() {
	int cex = vshSblAimgrIsCEX();	
	int dex = vshSblAimgrIsDEX();
	//int test = vshSblAimgrIsTest(); /*testkits will show as DEX :/ */
	int tool = vshSblAimgrIsTool();
	
	int idu = vshSysconIsIduMode();
	int show = vshSysconIsShowMode();
	
	//version spoofing side effect fix here
	if ( cex == dex ) {
		int ret = 0;
		int val = -1;

		ret = sceRegMgrGetKeyInt("/CONFIG/SYSTEM", "debug_mode", &val); //test&dex-registry only
		//ret = sceRegMgrGetKeyInt("/DEVENV/TOOL/", "machine_type", &val); //tool-registry only
	
		if (ret < 0) {
			if ( idu ) {
				return "CEX (IDU)";
			} else {
				return "CEX";
			}
		}	
		return "Test/Dev Kit";
	}

	
	//Normal detection
	if ( cex ) {
		if ( idu ) {
			return "CEX (IDU)";
		} else {
			return "CEX";
		}
	/*} else if ( test ) {
		if ( show ) {
			return "Testing Kit (Show Mode)";
		} else {
			return "Testing Kit";
		}*/
	} else if ( dex ) {
		if ( show ) {
			return "Test/Dev Kit (Show Mode)";
		} else {
			return "Test/Dev Kit";
		}
	} else if ( tool ) {
		return "Tool";
	} else {
		return "error";
	}
}

//! Hardware Info
int _vshSysconGetHardwareInfo(char HARD[4]);
int _vshSysconGetHardwareInfo2(char HARD[4]);

void getHardware() {
	
	int i = 0;
	char HARD[4] = {};
	
	_vshSysconGetHardwareInfo(HARD);
	
	for (i = 0; i < 4; i++) {
		printf("%02X ", HARD[i]);
	}
}

void getHardware2() {
	
	int i = 0;
	char HARD[4] = {};
	
	_vshSysconGetHardwareInfo2(HARD);
	
	for (i = 0; i < 4; i++) {
		printf("%02X ", HARD[i]);
	}
}


/********************* converting functions *********************************/

const char* convert_button_assign(int button_assign) {
	switch ( button_assign ) {
		case 0: return "O = Enter";
		case 1: return "X = Enter";
		default: return "Unknown layout!?";
	}
}

const char* convert_language(int language) {
	switch ( language ) {
		case 0: return "Japanese";
		case 1: return "English US";
		case 2: return "French";
		case 3: return "Spanish";
		case 4: return "German";
		case 5: return "Italian";
		case 6: return "Dutch"; 
		case 7: return "Portuguese";
		case 8: return "Russian"; 
		case 9: return "Korean"; 
		case 10: return "Traditional Chinese";
		case 11: return "Simplified Chinese";
		case 12: return "Finnish";
		case 13: return "Swedish"; 
		case 14: return "Danish";
		case 15: return "Norwegian";
		case 16: return "Polish";
		case 17: return "Brazilian Portuguese";
		case 18: return "English UK";
		default: return "Unknown layout!?";
	}
}

const char* convert_model(int model) {
	char fat_string[] = "D4:4B:5E";
	
	switch ( model ) {
		case 65536: //0x10000
			//Fat or Slim by MAC Address until theres a better solution
			if(strstr(mac_string, fat_string)) {
				return "Vita Fat";
			} else {
				return "Vita Slim";
			}
		case 131072: return "PlayStation TV"; //0x20000
		default: return "Unknown model!?";
	}
}

void convertdat(char* buff){

	char mid_string[] = "MID=";
	char dig_string[] = "DIG=";
	char did_string[] = "DID=";
	char aid_string[] = "AID=";
	char oid_string[] = "OID=";
	char svr_string[] = "SVR=";
	
	char delimiter[] = "=";
	char *ptr;
	
	//printf("buffer: %s\n", buff);
	
	if(strstr(buff, mid_string)) {	
		//printf("needle found!");
		ptr = strtok(buff, delimiter);
		//printf("prefix %s\n", ptr);
		ptr = strtok(NULL, delimiter);	//next part..
		//printf("mid found %s\n", ptr);
		strncpy(mid, ptr, 50);
		//printf("Mid is %s\n", ptr);
		
	} else if(strstr(buff, dig_string)) {
		ptr = strtok(buff, delimiter);
		ptr = strtok(NULL, delimiter);	
		strncpy(dig, ptr, 50);		
		
	} else if(strstr(buff, did_string)) {
		ptr = strtok(buff, delimiter);
		ptr = strtok(NULL, delimiter);	
		strncpy(did, ptr, 50);		
		
	} else if(strstr(buff, aid_string)) {
		ptr = strtok(buff, delimiter);
		ptr = strtok(NULL, delimiter);	
		strncpy(aid, ptr, 50);		
		
	} else if(strstr(buff, oid_string)) {
		ptr = strtok(buff, delimiter);
		ptr = strtok(NULL, delimiter);	
		strncpy(oid, ptr, 50);		
		
	} else if(strstr(buff, svr_string)) {
		ptr = strtok(buff, delimiter);
		ptr = strtok(NULL, delimiter);	
		strncpy(svr, ptr, 50);		
		
	}else {
		printf("ux0:id.dat wrongly formatted?\n"); 
	}
}

//thx TheFloW!
void getSizeString(char *string, uint64_t size) {
	double double_size = (double)size;

	int i = 0;
	static char *units[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
	while (double_size >= 1024.0f) {
		double_size /= 1024.0f;
		i++;
	}

	sprintf(string, "%.*f %s", (i == 0) ? 0 : 2, double_size, units[i]);
}


/****************************** Registry functions ****************************************/

///type02 - int
int getInteger(const char* location, const char* value) {
	int val = -1;
	int ret = 0;
	
	ret = sceRegMgrGetKeyInt(location, value, &val);
	
	if (ret < 0) {
		psvDebugScreenSetFgColor(RED);	
		printf("Failed to GetKeyInt: 0x%x ", ret);
		psvDebugScreenSetFgColor(WHITE);	
	}	
	return val;
}

///type03 - string
char* getString( const char* reg, const char* key ) {
	int ret = 0;
	static char string[128];
	
	ret = sceRegMgrGetKeyStr(reg, key, string, sizeof(string)); 
	
	if (ret < 0) {
		psvDebugScreenSetFgColor(RED);	
		printf("Failed to GetKeyStr: 0x%x ", ret);
		psvDebugScreenSetFgColor(WHITE);	
	}	
	return string;
}


///read the region_no int by manually reading out system.dreg :/
const char* get_region_no() {
	
	FILE *fp = NULL;
    unsigned char hex[1024] = "";

    if ( ( fp = fopen ( "vd0:registry/system.dreg", "rb")) == NULL) {
        printf ( "Could not open vd0:registry/system.dreg\n");
        return "";
    }

	fseek(fp, 92, SEEK_SET);
	fread ( &hex, 1, 1, fp);
	fclose(fp);
		
	switch ( hex[0] ) {
		case 0: return "0";
		case 1: return "Japan";				//PCH-X000
		case 2: return "North America"; 	//PCH-X001
		case 3: return "Australia";			//PCH-x002 
		case 4: return "United Kingdom"; 	//PCH-x003
		case 5: return "Europe"; 			//PCH-X004
		case 6: return "Korea";				//
		case 7: return "Asia"; 				//
		case 8: return "Taiwan";			//
		case 9: return "Russia"; 			//PCH-X008
		case 10: return "Mexico"; 			//
		case 11: return "msg_off"; 		
		case 12: return "12"; 
		case 13: return "China"; 			//
		case 14: return "14"; 
		case 15: return "15"; 
		default: return "Unknown layout!?";
	}
}


/******************** Battery functions **********************************/

const char* batteryStatus() {
    if (!scePowerIsBatteryCharging()) return "In use";
    else return "Charging";
}

int batteryRemCapacity(){
	char mAh[10];
	sprintf(mAh,"%i",scePowerGetBatteryRemainCapacity());
	int cap = atoi(mAh);	
	return cap;
}
int batteryCapacity(){
	char mAh[10];
	sprintf(mAh,"%i",scePowerGetBatteryFullCapacity());
	int cap = atoi(mAh);	
	return cap;
}

char* batteryPercentage() {
	static char percentage[5];
	sprintf(percentage, "%d%%", scePowerGetBatteryLifePercent());
	return percentage;
}

char* getBatteryVoltage() {
	static char voltage[8];
	sprintf(voltage,"%0.2f",(float)scePowerGetBatteryVolt() / 1000.0);
	return voltage;
}

char* getBatteryTemp() {
	static char temp[8];
	sprintf(temp,"%0.2f",(float)scePowerGetBatteryTemp() / 100.0);
	return temp;
}

/********************* initiating NET Modules for MAC *********************************/

void oslLoadNetModules(){
    if (sceSysmoduleIsLoaded(SCE_SYSMODULE_HTTP) != SCE_SYSMODULE_LOADED)
        sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
 
    if (sceSysmoduleIsLoaded(SCE_SYSMODULE_NET) != SCE_SYSMODULE_LOADED)
        sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
}
 
int initnet(){
    oslLoadNetModules();
	
	int ret;
 
    SceNetInitParam initparam;
    net_memory = malloc(1*1024*1024);
    initparam.memory = net_memory;
    initparam.size = NET_INIT_SIZE;
    initparam.flags = 0;
 
    ret = sceNetInit(&initparam);
    if(ret < 0){ // Error
        free(net_memory);
        net_memory = NULL;
        return -1;
    } else { // Exit
        ret = sceNetCtlInit();
        if (ret < 0 && ret != NET_CTL_ERROR_NOT_TERMINATED){ // Error
            sceNetTerm();
            free(net_memory);
            net_memory = NULL;
            return -2;
        }
    }
    return 0;
}
/*char* getMac() {	
	static SceNetEtherAddr mac;
    static char mac_string[32];
		
	sceNetGetMacAddress(&mac, 0);
	
	sprintf(mac_string, "%02X:%02X:%02X:%02X:%02X:%02X", mac.data[0], mac.data[1], mac.data[2], mac.data[3], mac.data[4], mac.data[5]);

    return mac_string;
}*/

/*****************************************************************************************************************************/


int main() {
	
	//init net & get mac
	initnet();
	sceNetGetMacAddress(&mac, 0);
	sprintf(mac_string, "%02X:%02X:%02X:%02X:%02X:%02X", mac.data[0], mac.data[1], mac.data[2], mac.data[3], mac.data[4], mac.data[5]);

	//init buttons
	SceCtrlData pad;
	SceCtrlData oldpad;
	oldpad.buttons = 0;
	memset(&pad, 0, sizeof(pad));
	
	//init screen
	psvDebugScreenInit();
		
	psvDebugScreenSetFgColor(GREEN);
	printf("PSVident v0.26\n\n\n");
	
	/////////////id.dat//////////////////
	FILE* f1 = fopen("ux0:id.dat", "r");
	
	if (f1 == NULL){
		psvDebugScreenSetFgColor(RED);
		printf("Error opening ux0:id.dat\n\n\n");
		
		if (f1 != NULL) fclose(f1);
	} else {
		while (fscanf(f1, "%s", buff) == 1) { // expect 1 successful conversion
			convertdat(buff);
		}	
		fclose(f1);
	}	
	psvDebugScreenSetFgColor(WHITE);		
	/////////////////////////////////////
	
	
	///Vita Model
	printf("* Vita model:           %s", convert_model(sceKernelGetModelForCDialog()));
	printf(" (0x%08X)\n", sceKernelGetModelForCDialog()); 
	
	///Vita Firmware
	SceSystemSwVersionParam sw_ver_param;
	sw_ver_param.size = sizeof(SceSystemSwVersionParam);
	sceKernelGetSystemSwVersion(&sw_ver_param);
	printf("* Kernel version:       %s %s\n", sw_ver_param.version_string, mode());
	printf("\n");
	
	///Mac Address
	printf("* MAC address:          %s\n\n", mac_string);
	
	///ConsoleID / IDPS
	printf("* IDPS:                 %s\n\n", getCID());
	
	/*VisibleID
	printf("* Visible ID:           %s\n", getVID());
	///SMI
	printf("* SMI:                  %s\n\n", getSMI());*/
	
	
	///Hardware Info 1
	/*printf("* Hardware Info 1       ");
	getHardware();
	printf("\n");

	///Hardware Info 2
	printf("* Hardware Info 2       ");
	getHardware2();
	printf("\n\n");	*/
	
	///Memory Card data
	uint64_t free_size = 0, max_size = 0;
	sceAppMgrGetDevInfo("ux0:", &max_size, &free_size);
	char free_size_string[16], max_size_string[16];
		getSizeString(free_size_string, free_size);
		getSizeString(max_size_string, max_size);
	psvDebugScreenSetFgColor(GREY);
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("MemoryCard:           %s / %s\n", free_size_string, max_size_string);
		
	
	printf("\n\nProcessor(s)\n\n");
	
	///Clock Speeds
	psvDebugScreenSetFgColor(YELLOW);
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("ARM Clock frequency:  %d MHz\n", getClockFrequency(0));
	psvDebugScreenSetFgColor(YELLOW);
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("BUS Clock frequency:  %d MHz\n", getClockFrequency(1));
	/*psvDebugScreenSetFgColor(YELLOW);
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("GPU Clock frequency:  %d MHz\n", getClockFrequency(2));*/
	
	
	
	if (scePowerIsBatteryExist()) {
		printf("\n\nBattery\n\n");
	
		///Battery %
		psvDebugScreenSetFgColor(RED);
		printf("* ");
		psvDebugScreenSetFgColor(WHITE);
		printf("Battery percentage:   %s\n", batteryPercentage());	
	
		///Battery Capacity
		psvDebugScreenSetFgColor(RED);
		printf("* ");
		psvDebugScreenSetFgColor(WHITE);
		printf("Battery capacity:     %i/%i mAh\n", batteryRemCapacity(), batteryCapacity());
	
		///Battery is charging?
		psvDebugScreenSetFgColor(RED);	
		printf("* ");
		psvDebugScreenSetFgColor(WHITE);
		printf("Battery status:       %s\n", batteryStatus());
	
		///Battery Lifetime
		psvDebugScreenSetFgColor(RED);
		printf("* ");
		psvDebugScreenSetFgColor(WHITE);
		printf("Battery lifetime:     %i minutes\n", scePowerGetBatteryLifeTime());
		
		///Battery Temperature
		psvDebugScreenSetFgColor(RED);
		printf("* ");
		psvDebugScreenSetFgColor(WHITE);
		printf("Battery temperatur:   %s Celsius\n", getBatteryTemp());
		
		///Battery Voltage
		psvDebugScreenSetFgColor(RED);
		printf("* ");
		psvDebugScreenSetFgColor(WHITE);
		printf("Battery voltage:      %s Volt\n", getBatteryVoltage());
		
		///Battery State of Health
		psvDebugScreenSetFgColor(RED);
		printf("* ");
		psvDebugScreenSetFgColor(WHITE);
		printf("State of Health:      %i%%\n", scePowerGetBatterySOH());					
	}

	printf("\n\nRegistry/Settings\n\n");
	
	///Registry: Vita username
	/*psvDebugScreenSetFgColor(CYAN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("username:             ");
	printf("%s\n", getString("/CONFIG/SYSTEM", "username"));*/
	
	///Registry: button_assign
	psvDebugScreenSetFgColor(CYAN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("button_assign:        ");
	printf("%s\n", convert_button_assign(getInteger("/CONFIG/SYSTEM", "button_assign")));
	
	///Registry: language
	psvDebugScreenSetFgColor(CYAN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("language:             ");
	printf("%s\n", convert_language(getInteger("/CONFIG/SYSTEM", "language")));
	
	
	///Registry: region_no
	psvDebugScreenSetFgColor(CYAN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("region_no:            ");
	printf("%s\n", get_region_no()); //reading manually from dreg

	
	///Registry: suspend_interval
	psvDebugScreenSetFgColor(CYAN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("suspend_interval:     ");
	printf("%i seconds\n", getInteger("/CONFIG/POWER_SAVING", "suspend_interval"));
	
	/*//Registry: Lockscreen Password
	psvDebugScreenSetFgColor(CYAN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("lockscreen passcode   ");
	printf("%s\n", getString("/SECURITY/SCREEN_LOCK", "passcode"));
	
	///Registry: Parental Password
	psvDebugScreenSetFgColor(CYAN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("parental passcode     ");
	printf("%s\n", getString("/SECURITY/PARENTAL", "passcode"));*/
	
	printf("\n\nPSN Account\n\n");
	
	///id.dat: PSN Username
	psvDebugScreenSetFgColor(GREEN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("PSN Nickname:         %s\n", oid);
	
	///Registry: psn email login_id
	psvDebugScreenSetFgColor(GREEN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("E-Mail:               ");
	printf("%s\n", getString("/CONFIG/NP", "login_id"));
	
	///Registry: psn account password
	psvDebugScreenSetFgColor(GREEN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("password:             ");
	printf("%s\n", getString("/CONFIG/NP", "password"));
	
	///id.dat: PSID
	psvDebugScreenSetFgColor(GREEN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("PSID:                 %s\n", did);
	
	///id.dat: account_id 
	psvDebugScreenSetFgColor(GREEN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("account_id:           ");
		//printf("%s\n", getBin("/CONFIG/NP", "account_id"));
	int i; ///reading and inversing from id.dat
	for (i = strlen(aid) - 1; i >= 0; i = i - 2){
		printf("%c", aid[i-1]);
		printf("%c", aid[i]);
	} printf("\n");
	
	
	///Registry: psn region
	psvDebugScreenSetFgColor(GREEN);	
	printf("* ");
	psvDebugScreenSetFgColor(WHITE);
	printf("region:               ");
	printf("%s\n", getString("/CONFIG/NP", "country"));
	
	
	///testing
	/*printf("\n\n");
	printf("vshSblAimgrIsCEX():           %i\n", vshSblAimgrIsCEX());	
	printf("vshSblAimgrIsDEX():           %i\n", vshSblAimgrIsDEX());	
	printf("vshSblAimgrIsDolce():         %i\n", vshSblAimgrIsDolce());	
	printf("vshSblAimgrIsGenuineDolce():  %i\n", vshSblAimgrIsGenuineDolce());	
	printf("vshSblAimgrIsGenuineVITA():   %i\n", vshSblAimgrIsGenuineVITA());	
	printf("vshSblAimgrIsTest():          %i\n", vshSblAimgrIsTest());	
	printf("vshSblAimgrIsTool():          %i\n", vshSblAimgrIsTool());	
	printf("vshSblAimgrIsVITA():          %i\n", vshSblAimgrIsVITA());	
	
	printf("vshSysconIsIduMode():         %i\n", vshSysconIsIduMode());
	printf("vshSysconIsShowMode():        %i\n", vshSysconIsShowMode());*/

	
	///show id.dat
	/*printf("\n\n\n\n\nid.dat\n---------------\n");
	printf("MID: %s\n", mid );
	printf("DIG: %s\n", dig );
	printf("DID: %s\n", did );
	printf("AID: %s\n", aid );
	printf("OID: %s\n", oid );
	printf("SVR: %s\n", svr );*/
	
	printf("\n\n\n");
	//printf("> Press X to make a screenshot\n\n");
	printf("> Press O to update values\n\n");
	printf("> Press Select + Start to exit..");
		
		
	while (1) {
		sceCtrlPeekBufferPositive(0, &pad, 1);
		
		///make Screenshot
		/*if (pad.buttons != oldpad.buttons) {
			if (pad.buttons & SCE_CTRL_CROSS) {
				
				//! Enable screnshot
				int res = sceScreenShotEnable();
 
				sceScreenShotSetParam photo;
				memset(&photo, 0, sizeof(sceScreenShotSetParam));
				photo.photoTitle = "test";
				photo.gameTitle = "TEST0001";
				photo.gameComment = "abc";
				sceScreenshotSetParam(&photo);
				
				sceScreenShotCapture("ux0:/pspemu/screenshot");
				sceScreenshotDisable();
			}	
		}*/
		
		///self reloading
		if (pad.buttons != oldpad.buttons) {
			if (pad.buttons & SCE_CTRL_CIRCLE) {
				sceAppMgrLoadExec("app0:/eboot.bin", NULL, NULL);
			}
		}	

		///exit combo
		if (pad.buttons & SCE_CTRL_SELECT && pad.buttons & SCE_CTRL_START)
			break;

		oldpad = pad;
	}

	sceKernelExitProcess(0);
	return 0;
}
