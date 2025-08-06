# indodax_api
This program uses the Indodax REST API, a proof of concept (POC) demonstrating that we can create and utilize a REST API with C.

If you'd like to give a gift, please send some DOGE to my wallet "D6ckQMfcWSosY7J4rNQkY1rKX1pQTmNuTt"

Or if you're an Indodax user, you can send it using my username "idban" without the quotation marks.

# Dependency
- libcurl4-openssl-dev 
- libssl-dev
- libjansson-dev

> [!NOTE]
> Deb family
> ```
> sudo apt-get install libcurl4-openssl-dev libssl-dev libjansson-dev
> ```
> RH family
> ```
> sudo yum install jansson-devel openssl-devel libcurl-devel
> ```

> [!CAUTION]
> if yuu are updating your openssl please make sure you also update oppenssh, otherwise your sshd will fail to start because dependency is different.

# Compile
use make 
or with cli command 
```
gcc -o indodax_api main.c -lcurl -lssl -lcrypto -ljansson
```
# Screenshot
main menu\
![Main menu](https://github.com/dump9x/indodax_api/blob/main/2025-08-06_10h54_55.png)\
\
open order\
![Open Order](https://github.com/dump9x/indodax_api/blob/main/2025-08-06_10h55_36.png)
