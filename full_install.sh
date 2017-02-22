sudo make shutdown && sudo make unload
sudo make uninstall
make clean
#./autogen.sh sudo ./configure && 
make CC=/usr/bin/gcc-4.9 && sudo make install CC=/usr/bin/gcc-4.9 && sudo make load && sudo make run
