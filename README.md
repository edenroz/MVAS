these are the commands for managing the multi-view Linux kernel module

all: just compile all the stuff

install: install the modules

load: mount the modules

run: activate the modules

shutdown: deactivate the modules

unload: umnount the modules

uninstall: install the modules

a correct usage would imply the following sequence of depending actions

all
install 
	[run
	 shutdown]
uninstall
