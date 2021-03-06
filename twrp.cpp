/*
		TWRP is free software: you can redistribute it and/or modify
		it under the terms of the GNU General Public License as published by
		the Free Software Foundation, either version 3 of the License, or
		(at your option) any later version.

		TWRP is distributed in the hope that it will be useful,
		but WITHOUT ANY WARRANTY; without even the implied warranty of
		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
		GNU General Public License for more details.

		You should have received a copy of the GNU General Public License
		along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include "gui/twmsg.h"

#include "cutils/properties.h"
#include "bootloader_message_twrp/include/bootloader_message_twrp/bootloader_message.h"

#ifdef ANDROID_RB_RESTART
#include "cutils/android_reboot.h"
#else
#include <sys/reboot.h>
#endif

extern "C" {
#include "gui/gui.h"
}
#include "set_metadata.h"
#include "gui/gui.hpp"
#include "gui/pages.hpp"
#include "gui/objects.hpp"
#include "twcommon.h"
#include "twrp-functions.hpp"
#include "data.hpp"
#include "partitions.hpp"
#include "openrecoveryscript.hpp"
#include "variables.h"
#include "twrpAdbBuFifo.hpp"
#ifdef TW_USE_NEW_MINADBD
#include "minadbd/minadbd.h"
#else
extern "C" {
#include "minadbd21/adb.h"
}
#endif
#include <list>
#include "sov.h"

//extern int adb_server_main(int is_daemon, int server_port, int /* reply_fd */);

TWPartitionManager PartitionManager;
int Log_Offset;
bool datamedia;

static void Print_Prop(const char *key, const char *name, void *cookie) {
	printf("%s=%s\n", key, name);
}
void lockCheck(){
	FILE *f;
	char hello[50];
	f=fopen("/sdcard/SHRP/data/slts","r");
	if(f==NULL){
		f=fopen("/twres/slts","r");
	}
	if(f!=NULL){
		fgets(hello,50,f);
		fclose(f);
		if(hello[0]=='1'){
			//Password Protected Recovery
			DataManager::SetValue("c_target_destination","c_pass_capture");
			DataManager::SetValue("lock_enabled",1);
			DataManager::SetValue("patt_lock_enabled",0);
			DataManager::SetValue("c_new",0);
			DataManager::SetValue("c_new_pattern",0);
			PartitionManager.Disable_MTP();
		}else if(hello[0]=='2'){
			//Pattern Protected Recovery
			DataManager::SetValue("c_target_destination","c_patt_capture");
			DataManager::SetValue("lock_enabled",1);
			DataManager::SetValue("patt_lock_enabled",1);
			DataManager::SetValue("c_new",0);
			DataManager::SetValue("c_new_pattern",0);
			//DataManager::SetValue("main_pass",1);
			PartitionManager.Disable_MTP();
		}else{
			//Unprotected Recovery
			DataManager::SetValue("c_target_destination","main2");
			DataManager::SetValue("lock_enabled",0);
			DataManager::SetValue("patt_lock_enabled",0);
			DataManager::SetValue("c_new",1);
			DataManager::SetValue("c_new_pattern",1);
		}
	}else{
		//Unprotected Recovery
		DataManager::SetValue("c_target_destination","main2");
		DataManager::SetValue("lock_enabled",0);
		DataManager::SetValue("patt_lock_enabled",0);
		DataManager::SetValue("c_new",1);
		DataManager::SetValue("c_new_pattern",1);
	}
}
void shrp_lockscreen_date(){//SHRP Buutiful Lockscreen Date View
	stringstream day;
	string Current_Date,month,week,main_result,day_s;
	time_t seconds = time(0);
	struct tm *t = localtime(&seconds);
	{
		string time;
		DataManager::GetValue("tw_ls_time",time);
		DataManager::SetValue("tw_ls_time",time.c_str());
	}
	int m=t->tm_mon+1;
	int y=t->tm_year+1900;
	int d=t->tm_mday;
	static int tmp[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	y -= m < 3;
	int w=( y + y / 4 - y / 100 + y / 400 + tmp[m - 1] + d) % 7;
	switch(t->tm_mon+1){
		case 1:month=" Jan";
		break;
		case 2:month=" Feb";
		break;
		case 3:month=" Mar";
		break;
		case 4:month=" Apr";
		break;
		case 5:month=" May";
		break;
		case 6:month=" Jun";
		break;
		case 7:month=" Jul";
		break;
		case 8:month=" Aug";
		break;
		case 9:month=" Sep";
		break;
		case 10:month=" Oct";
		break;
		case 11:month=" Nov";
		break;
		case 12:month=" Dec";
		break;
	}
	switch(w){
		case 0:week="Sun, ";
		break;
		case 1:week="Mon, ";
		break;
		case 2:week="Tue, ";
		break;
		case 3:week="Wed, ";
		break;
		case 4:week="Thu, ";
		break;
		case 5:week="Fri, ";
		break;
		case 6:week="Sat, ";
		break;
	}
	day<<t->tm_mday;
	day>>day_s;
	main_result=week+day_s+month;
	DataManager::SetValue("c_lock_screen_date",main_result);
}
bool checkOffical(string target){
    for(auto it=devices.begin();it!=devices.end();it++){
		if(target==*it){
		    return true;
		}
	}
	return false;
}
void disp_info(){
	string tmp;
	gui_msg(Msg("|SKYHAWK RECOVERY PROJECT",0));
	DataManager::GetValue("shrp_ver",tmp);
	tmp="|Version - "+tmp;
	gui_msg(Msg(tmp.c_str(),0));
	if(checkOffical(DataManager::GetStrValue("device_code_name"))){
		tmp="|Status - Official";
	}else{
		tmp="|Status - Unofficial";
	}
	gui_msg(Msg(tmp.c_str(),0));
	DataManager::GetValue("device_code_name",tmp);
	tmp="|Device - "+tmp;
	gui_msg(Msg(tmp.c_str(),0));
}

int main(int argc, char **argv) {
	// Recovery needs to install world-readable files, so clear umask
	// set by init
	umask(0);

	Log_Offset = 0;

	// Set up temporary log file (/tmp/recovery.log)
	freopen(TMP_LOG_FILE, "a", stdout);
	setbuf(stdout, NULL);
	freopen(TMP_LOG_FILE, "a", stderr);
	setbuf(stderr, NULL);

	signal(SIGPIPE, SIG_IGN);

	// Handle ADB sideload
	if (argc == 3 && strcmp(argv[1], "--adbd") == 0) {
		property_set("ctl.stop", "adbd");
#ifdef TW_USE_NEW_MINADBD
		//adb_server_main(0, DEFAULT_ADB_PORT, -1); TODO fix this for android8
		minadbd_main();
#else
		adb_main(argv[2]);
#endif
		return 0;
	}

#ifdef RECOVERY_SDCARD_ON_DATA
	datamedia = true;
#endif

	char crash_prop_val[PROPERTY_VALUE_MAX];
	int crash_counter;
	property_get("twrp.crash_counter", crash_prop_val, "-1");
	crash_counter = atoi(crash_prop_val) + 1;
	snprintf(crash_prop_val, sizeof(crash_prop_val), "%d", crash_counter);
	property_set("twrp.crash_counter", crash_prop_val);
	property_set("ro.twrp.boot", "1");
	property_set("ro.twrp.version", TW_VERSION_STR);

	time_t StartupTime = time(NULL);
	printf("Starting TWRP %s-%s on %s (pid %d)\n", TW_VERSION_STR, TW_GIT_REVISION, ctime(&StartupTime), getpid());

	// Load default values to set DataManager constants and handle ifdefs
	DataManager::SetDefaultValues();
	printf("Starting the UI...\n");
	gui_init();
	disp_info();
	printf("=> Linking mtab\n");
	symlink("/proc/mounts", "/etc/mtab");
	std::string fstab_filename = "/etc/twrp.fstab";
	if (!TWFunc::Path_Exists(fstab_filename)) {
		fstab_filename = "/etc/recovery.fstab";
	}
	printf("=> Processing %s\n", fstab_filename.c_str());
	if (!PartitionManager.Process_Fstab(fstab_filename, 1)) {
		LOGERR("Failing out of recovery due to problem with fstab.\n");
		return -1;
	}
	PartitionManager.Output_Partition_Logging();
	// Load up all the resources
	gui_loadResources();
	//SHRP_initial_funcs
	shrp_lockscreen_date();
	lockCheck();

	bool Shutdown = false;
	bool SkipDecryption = false;
	string Send_Intent = "";
	{
		TWPartition* misc = PartitionManager.Find_Partition_By_Path("/misc");
		if (misc != NULL) {
			if (misc->Current_File_System == "emmc") {
				set_misc_device(misc->Actual_Block_Device.c_str());
			} else {
				LOGERR("Only emmc /misc is supported\n");
			}
		}
		get_args(&argc, &argv);

		int index, index2, len;
		char* argptr;
		char* ptr;
		printf("Startup Commands: ");
		for (index = 1; index < argc; index++) {
			argptr = argv[index];
			printf(" '%s'", argv[index]);
			len = strlen(argv[index]);
			if (*argptr == '-') {argptr++; len--;}
			if (*argptr == '-') {argptr++; len--;}
			if (*argptr == 'u') {
				ptr = argptr;
				index2 = 0;
				while (*ptr != '=' && *ptr != '\n')
					ptr++;
				// skip the = before grabbing Zip_File
				while (*ptr == '=')
					ptr++;
				if (*ptr) {
					string ORSCommand = "install ";
					ORSCommand.append(ptr);

					// If we have a map of blocks we don't need to mount data.
					SkipDecryption = *ptr == '@';

					if (!OpenRecoveryScript::Insert_ORS_Command(ORSCommand))
						break;
				} else
					LOGERR("argument error specifying zip file\n");
			} else if (*argptr == 'w') {
				if (len == 9) {
					if (!OpenRecoveryScript::Insert_ORS_Command("wipe data\n"))
						break;
				} else if (len == 10) {
					if (!OpenRecoveryScript::Insert_ORS_Command("wipe cache\n"))
						break;
				}
				// Other 'w' items are wipe_ab and wipe_package_size which are related to bricking the device remotely. We will not bother to suppor these as having TWRP probably makes "bricking" the device in this manner useless
			} else if (*argptr == 'n') {
				DataManager::SetValue(TW_BACKUP_NAME, gui_parse_text("{@auto_generate}"));
				if (!OpenRecoveryScript::Insert_ORS_Command("backup BSDCAE\n"))
					break;
			} else if (*argptr == 'p') {
				Shutdown = true;
			} else if (*argptr == 's') {
				if (strncmp(argptr, "send_intent", strlen("send_intent")) == 0) {
					ptr = argptr + strlen("send_intent") + 1;
					Send_Intent = *ptr;
				} else if (strncmp(argptr, "security", strlen("security")) == 0) {
					LOGINFO("Security update\n");
				} else if (strncmp(argptr, "sideload", strlen("sideload")) == 0) {
					if (!OpenRecoveryScript::Insert_ORS_Command("sideload\n"))
						break;
				} else if (strncmp(argptr, "stages", strlen("stages")) == 0) {
					LOGINFO("ignoring stages command\n");
				}
			} else if (*argptr == 'r') {
				if (strncmp(argptr, "reason", strlen("reason")) == 0) {
					ptr = argptr + strlen("reason") + 1;
					gui_print("%s\n", ptr);
				}
			}
		}
		printf("\n");
	}

	if (crash_counter == 0) {
		property_list(Print_Prop, NULL);
		printf("\n");
	} else {
		printf("twrp.crash_counter=%d\n", crash_counter);
	}

	// Check for and run startup script if script exists
	TWFunc::check_and_run_script("/sbin/runatboot.sh", "boot");
	TWFunc::check_and_run_script("/sbin/postrecoveryboot.sh", "boot");
/*#ifdef TW_INCLUDE_INJECTTWRP
	// Back up TWRP Ramdisk if needed:
	TWPartition* Boot = PartitionManager.Find_Partition_By_Path("/boot");
	LOGINFO("Backing up TWRP ramdisk...\n");
	if (Boot == NULL || Boot->Current_File_System != "emmc")
		TWFunc::Exec_Cmd("injecttwrp --backup /tmp/backup_recovery_ramdisk.img");
	else {
		string injectcmd = "injecttwrp --backup /tmp/backup_recovery_ramdisk.img bd=" + Boot->Actual_Block_Device;
		TWFunc::Exec_Cmd(injectcmd);
	}
	LOGINFO("Backup of TWRP ramdisk done.\n");
#endif
*/
	// Offer to decrypt if the device is encrypted
	if (DataManager::GetIntValue(TW_IS_ENCRYPTED) != 0) {
		if (SkipDecryption) {
			LOGINFO("Skipping decryption\n");
		} else {
			LOGINFO("Is encrypted, do decrypt page first\n");
			if (gui_startPage("decrypt", 1, 1) != 0) {
				LOGERR("Failed to start decrypt GUI page.\n");
			} else {
				// Check for and load custom theme if present
				TWFunc::check_selinux_support();
				gui_loadCustomResources();
			}
		}
	} else if (datamedia) {
		TWFunc::check_selinux_support();
		if (tw_get_default_metadata(DataManager::GetSettingsStoragePath().c_str()) != 0) {
			LOGINFO("Failed to get default contexts and file mode for storage files.\n");
		} else {
			LOGINFO("Got default contexts and file mode for storage files.\n");
		}
	}

	// Fixup the RTC clock on devices which require it
	if (crash_counter == 0)
		TWFunc::Fixup_Time_On_Boot();

	// Read the settings file
	TWFunc::Update_Log_File();
	DataManager::ReadSettingsFile();
	PageManager::LoadLanguage(DataManager::GetStrValue("tw_language"));
	GUIConsole::Translate_Now();

	// Run any outstanding OpenRecoveryScript
	std::string cacheDir = TWFunc::get_cache_dir();
	std::string orsFile = cacheDir + "/recovery/openrecoveryscript";
	if ((DataManager::GetIntValue(TW_IS_ENCRYPTED) == 0 || SkipDecryption) && (TWFunc::Path_Exists(SCRIPT_FILE_TMP) || TWFunc::Path_Exists(orsFile))) {
		OpenRecoveryScript::Run_OpenRecoveryScript();
	}

#ifdef TW_HAS_MTP
	char mtp_crash_check[PROPERTY_VALUE_MAX];
	property_get("mtp.crash_check", mtp_crash_check, "0");
	if (DataManager::GetIntValue("tw_mtp_enabled")
			&& !strcmp(mtp_crash_check, "0") && !crash_counter
			&& (!DataManager::GetIntValue(TW_IS_ENCRYPTED) || DataManager::GetIntValue(TW_IS_DECRYPTED))) {
		property_set("mtp.crash_check", "1");
		LOGINFO("Starting MTP\n");
		if (!PartitionManager.Enable_MTP())
			PartitionManager.Disable_MTP();
		else
			gui_msg("mtp_enabled=MTP Enabled");
		property_set("mtp.crash_check", "0");
	} else if (strcmp(mtp_crash_check, "0")) {
		gui_warn("mtp_crash=MTP Crashed, not starting MTP on boot.");
		DataManager::SetValue("tw_mtp_enabled", 0);
		PartitionManager.Disable_MTP();
	} else if (crash_counter == 1) {
		LOGINFO("TWRP crashed; disabling MTP as a precaution.\n");
		PartitionManager.Disable_MTP();
	}
#endif

#ifndef TW_OEM_BUILD
	// Check if system has never been changed
	TWPartition* sys = PartitionManager.Find_Partition_By_Path(PartitionManager.Get_Android_Root_Path());
	TWPartition* ven = PartitionManager.Find_Partition_By_Path("/vendor");

	if (sys) {
		if ((DataManager::GetIntValue("tw_mount_system_ro") == 0 && sys->Check_Lifetime_Writes() == 0) || DataManager::GetIntValue("tw_mount_system_ro") == 2) {
			if (DataManager::GetIntValue("tw_never_show_system_ro_page") == 0) {
				DataManager::SetValue("tw_back", "main");
				if (gui_startPage("system_readonly", 1, 1) != 0) {
					LOGERR("Failed to start system_readonly GUI page.\n");
				}
			} else if (DataManager::GetIntValue("tw_mount_system_ro") == 0) {
				sys->Change_Mount_Read_Only(false);
				if (ven)
					ven->Change_Mount_Read_Only(false);
			}
		} else if (DataManager::GetIntValue("tw_mount_system_ro") == 1) {
			// Do nothing, user selected to leave system read only
		} else {
			sys->Change_Mount_Read_Only(false);
			if (ven)
				ven->Change_Mount_Read_Only(false);
		}
	}
#endif
	twrpAdbBuFifo *adb_bu_fifo = new twrpAdbBuFifo();
	adb_bu_fifo->threadAdbBuFifo();

	// Launch the main GUI
	gui_start();

#ifndef TW_OEM_BUILD
	// Disable flashing of stock recovery
	TWFunc::Disable_Stock_Recovery_Replace();
#endif

	// Reboot
	TWFunc::Update_Intent_File(Send_Intent);
	delete adb_bu_fifo;
	TWFunc::Update_Log_File();
	gui_msg(Msg("rebooting=Rebooting..."));
	string Reboot_Arg;
	DataManager::GetValue("tw_reboot_arg", Reboot_Arg);
	if (Reboot_Arg == "recovery")
		TWFunc::tw_reboot(rb_recovery);
	else if (Reboot_Arg == "poweroff")
		TWFunc::tw_reboot(rb_poweroff);
	else if (Reboot_Arg == "bootloader")
		TWFunc::tw_reboot(rb_bootloader);
	else if (Reboot_Arg == "download")
		TWFunc::tw_reboot(rb_download);
	else if (Reboot_Arg == "edl")
		TWFunc::tw_reboot(rb_edl);
	else
		TWFunc::tw_reboot(rb_system);

	return 0;
}
