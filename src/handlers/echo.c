#include <ctype.h> //for toupper()

#include "../../include/handlers/echo.h"


void echo_handle(conn_t* client){
    char buffer[4096];

    while(1){
        ssize_t bytes_read = conn_read(client, buffer, sizeof(buffer)); // read untill buffer size

        if(bytes_read <= 0){
            break; // 0 and -1 means error with reading
        }

        // iterate through the recieved bytes, convert them to string and then uppercase
        for(ssize_t i = 0; i < bytes_read; i++){
            // cast to unsigned char to prevent undefined behavior with bytes.
            buffer[i] = toupper((unsigned char)buffer[i]);
        }

        //send modified buffer back to client
        if(conn_write_all(client, buffer, bytes_read) < 0){
            break; //failed to write
        }
    }

}