## THIS IS A GENERATED FILE -- DO NOT EDIT
.configuro: .libraries,em3 linker.cmd package/cfg/app_pem3.oem3

linker.cmd: package/cfg/app_pem3.xdl
	$(SED) 's"^\"\(package/cfg/app_pem3cfg.cmd\)\"$""\"M:/ECE3849/Lab3/ECE3849d15_Lab3CAN_Cmrypkema_sdmichelin/.config/xconfig_app/\1\""' package/cfg/app_pem3.xdl > $@
