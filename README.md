gFTP
====

> gFTP is an FTP plugin for Geany on Linux.  
> Similar to NppFTP for Notepad++ on Windows.

The source code is chaos and difficult to maintain. Therefore, this project is **NO LONGER BEING MAINTAINED**. Everyone is encouraged to rewrite the code or make a similar plugin for Geany.


Features
--------
 * FTP profiles.
 * Download -> Edit -> Save -> Upload.
 * SFTP/TLSv1/SSLv3 authentication.
 * Cache directory listings.
 * Index and search files.
 * Proxy profiles.
 * Import NppFTP/FileZilla profiles.


Usage
-----
You may need to add a profile first. Fill the basic info like hostname, username and password. You can leave the username and password blank, and it will prompt for them when you log in. Passwords are simply encrypted. If you care about your FTP account security, you can choose SFTP authentication (if your FTP server supports) or leave password blank. gFTP currently supports SFTP/TLSv1/SSLv3 authenticaiton. When you choose SFTP, you can specify where the private and public key located. gFTP doesn't understand and can't generate Putty's .ppk file right now. You can leave it blank to use default keys located in ~/.ssh folder.

When you connect to a FTP server, gFTP may navigate to the 'Remote' directory for you. When you double click a file, it will be downloaded to a temporary folder (/tmp/gFTP/). For example, when you connect to ftp.mozilla.org anonymously and open the README file in the root folder, it will be downloaded to /tmp/gFTP/anonymous@ftp.mozilla.org/README and opened in Geany if Geany supports reading this kind of file. If you specify the 'Local' directory, files will be downloaded to that directory.


Requirements
------------
 * [libcurl4-openssl-dev 7.21+](http://curl.haxx.se/download.html)
 * libgtk2.0-dev
 * libxml2-dev


Install (Ubuntu/Debian)
-----------------------
    $ sudo add-apt-repository "deb http://ftp.cn.debian.org/debian squeeze main"
    $ sudo apt-get update
    $ sudo apt-get install gcc geany libgtk2.0-dev libcurl4-openssl-dev libxml2-dev
    $ cd src
    $ make && make install && make clean

Note: SFTP protocol is not supported in libcurl3 Ubuntu packages, therefore, you
may receive the error 'Protocol sftp not supported or disabled in libcurl', it is
recommended you install libcurl3 from Debian packages (see troubleshoot below).


Install (Fedora)
----------------
    $ sudo yum -y install gcc geany-devel libxml2-devel libcurl-devel openssl-devel
    $ cd src
    $ make && make install && make clean


Install (FreeBSD)
-----------------
    $ su -
    $ pkg_add -r geany gmake
    $ cd src
    $ gmake && gmake install && gmake clean


Install (Mac OS X)
------------------
Install Homebrew first. Then install Geany and other necessary libraries.

    brew install geany

Define environment variable ``PKG_CONFIG_PATH`` and run gcc to make shared object.

    export PKG_CONFIG_PATH=/opt/X11/lib/pkgconfig:/usr/local/opt/curl/lib/pkgconfig:/usr/local/Cellar/geany/1.22/lib/pkgconfig:/usr/local/opt/libxml2/lib/pkgconfig:/usr/local/opt/openssl/lib/pkgconfig
    gcc -shared -Wall -fPIC -o "gFTP.so" "gFTP.c" `pkg-config --cflags --libs gtk+-2.0 libcurl geany libxml-2.0 openssl`

Modify this plugin in Geany
---------------------------
 * Open gFTP.c in Geany and click Build > Set Build Commands.
   In C commands (Build), type 
``gcc -shared -Wall -fPIC -o "%e.so" "%f" `pkg-config libxml-2.0 --cflags --libs libcurl geany` ``
   
 * Set the Extra Plugin Path (Edit > Preference > General) to where the new gFTP.so is built. To test, press F9 or click Build > Build.
   
 * Just run a new Geany instance to test it. To get more info, run ``geany -v`` in terminal instead. To debug, install gdb and run ``gdb geany`` in terminal.


Test FTP locally
----------------
To test FTP or that with TLS/SSL authentication locally, use vsftpd. Here's a sample configuration for vsftpd:

    $ sudo mkdir /etc/vsftpd/
    $ cd /etc/vsftpd/
    $ sudo /usr/bin/openssl req -x509 -nodes -days 365 -newkey rsa:1024 -keyout vsftpd.pem -out vsftpd.pem

Then, you may need to provide information for the certificate. After that, edit '/etc/vsftpd.conf'. Uncomment several necessary lines, then append or modify these lines:

    ssl_enable=YES
    allow_anon_ssl=YES
    force_local_data_ssl=YES
    force_local_logins_ssl=YES
    ssl_tlsv1=YES
    ssl_sslv2=NO
    ssl_sslv3=NO
    ssl_ciphers=HIGH
    rsa_cert_file=/etc/vsftpd/vsftpd.pem

Comment or remove 'rsa_private_key_file' option or set it the same as 'rsa_cert_file'. If you want to set the port number, append this: listen_port=123 (e.g.). Restart service:

    $ sudo service vsftpd restart
   
To test SFTP, use OpenSSH.


Troubleshoot
------------
Error 'Protocol sftp not supported or disabled in libcurl'.

    $ sudo add-apt-repository "deb http://ftp.cn.debian.org/debian wheezy main"
    $ sudo apt-get update
    $ sudo apt-cache policy libcurl3

if the candidate is from http://ftp.cn.debian.org/... , you can run:

    $ sudo apt-get install libcurl3

if NOT, you can install the unstable version:

    -> $ sudo add-apt-repository "deb http://ftp.cn.debian.org/debian sid main"
    -> $ sudo apt-get update && sudo apt-get install libcurl3

or choose to install, for example, the 7.26.0-1 version from the version table:

    -> $ sudo apt-get install libcurl3=7.26.0-1

Another option is to build from source. (http://curl.haxx.se/download.html)

    $ sudo apt-get purge libcurl3
    $ sudo apt-get install libssl-dev libssh2-1-dev
    $ curl -O http://curl.haxx.se/download/curl-7.28.1.tar.gz
    $ tar xfvz curl-7.28.1.tar.gz
    $ cd curl-7.28.1
    $ ./configure --with-ssl --with-libssh2
    $ make
    $ sudo make install
    $ make clean

Then rebuild gFTP.


Contact developers
------------------
 * E-mail: caiguanhao@gmail.com
