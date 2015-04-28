# invoke SourceDir generated makefile for app.pem3
app.pem3: .libraries,app.pem3
.libraries,app.pem3: package/cfg/app_pem3.xdl
	$(MAKE) -f M:\Git\ECEWork\Lab2\Lab2_Cmrypkema_SdMichelini/src/makefile.libs

clean::
	$(MAKE) -f M:\Git\ECEWork\Lab2\Lab2_Cmrypkema_SdMichelini/src/makefile.libs clean

