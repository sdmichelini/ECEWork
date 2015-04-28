# invoke SourceDir generated makefile for app.pem3
app.pem3: .libraries,app.pem3
.libraries,app.pem3: package/cfg/app_pem3.xdl
	$(MAKE) -f M:\3849\ECE3849d15_Lab3CAN_Cmrypkema_sdmichelin/src/makefile.libs

clean::
	$(MAKE) -f M:\3849\ECE3849d15_Lab3CAN_Cmrypkema_sdmichelin/src/makefile.libs clean

