#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <ctype.h>
#include <jansson.h>

#define MAX_PAYLOAD 512
#define MAX_HEADER 256
#define CONFIG_PATH "indodax_config.txt"
#define MAX_LINE 128

void p_head() {
    printf(" _   ___   _      __    ___   _  \n");
    printf("| | | | \\ \\ \\_/  / /\\  | |_) | | \n");
    printf("|_| |_|_/ /_/ \\ /_/--\\ |_|   |_| \n");
    printf("indodax api v.001\n\n");
}

struct MemoryStruct {
    char *memory;
    size_t size;
};

void hmac_sha512(const char *data, const char *key, char *out_hex) {
    unsigned char *digest;
    digest = HMAC(EVP_sha512(), key, strlen(key), (unsigned char*)data, strlen(data), NULL, NULL);
    for (int i = 0; i < 64; i++) {
        sprintf(&out_hex[i*2], "%02x", (unsigned int)digest[i]);
    }
}

int read_config(const char *path, char **key, char **secret) {
    FILE *file = fopen(path, "r");
    if (!file) {
        perror("Error opening config file");
        return 0;
    }

    char line[MAX_LINE];
    *key = NULL;
    *secret = NULL;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "key=", 4) == 0) {
            line[strcspn(line, "\n")] = 0;
            *key = strdup(line + 4);
        }
        else if (strncmp(line, "secret=", 7) == 0) {
            line[strcspn(line, "\n")] = 0;
            *secret = strdup(line + 7);
        }
    }
    fclose(file);

    if (!*key || !*secret) {
        fprintf(stderr, "Config file missing key or secret\n");
        free(*key);
        free(*secret);
        return 0;
    }
    return 1;
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        fprintf(stderr, "Memory allocation error\n");
        return 0;
    }
    
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    
    return realsize;
}

char* extract_coin_name(const char *coin_pair) {
    if (!coin_pair) return strdup("N/A");
    
    const char *underscore = strchr(coin_pair, '_');
    if (underscore && (strcmp(underscore, "_idr") == 0)) {
        size_t len = underscore - coin_pair;
        char *coin_name = malloc(len + 1);
        if (coin_name) {
            strncpy(coin_name, coin_pair, len);
            coin_name[len] = '\0';
            return coin_name;
        }
    }
    return strdup(coin_pair);
}

double json_value_to_double(json_t *value) {
    if (json_is_real(value)) {
        return json_real_value(value);
    } else if (json_is_integer(value)) {
        return (double)json_integer_value(value);
    } else if (json_is_string(value)) {
        return atof(json_string_value(value));
    }
    return 0.0;
}

void format_orders_table(const char *json_response, const char *coin_pair) {
    json_error_t error;
    json_t *root = json_loads(json_response, 0, &error);
    if (!root) {
        fprintf(stderr, "JSON error: %s\n", error.text);
        return;
    }
    
    json_t *success = json_object_get(root, "success");
    if (!json_is_integer(success)) {
        fprintf(stderr, "Invalid success field\n");
        json_decref(root);
        return;
    }
    
    if (json_integer_value(success)) {
        json_t *return_obj = json_object_get(root, "return");
        if (!return_obj) {
            fprintf(stderr, "Missing 'return' object\n");
            json_decref(root);
            return;
        }
        
        json_t *orders = json_object_get(return_obj, "orders");
        if (!orders) {
            fprintf(stderr, "Missing 'orders' object\n");
            json_decref(root);
            return;
        }
        
        printf("+------------+-----------------+-------------------+-----------------------------+\n");
        printf("| Coin Name  | Price           | Open/Remain Order | Client Order ID             |\n");
        printf("+------------+-----------------+-------------------+-----------------------------+\n");
        
        if (json_is_object(orders)) {
            const char *coin_pair_key;
            json_t *order_array;
            json_object_foreach(orders, coin_pair_key, order_array) {
                if (!json_is_array(order_array)) continue;
                
                char *coin_name = extract_coin_name(coin_pair_key);
                
                size_t index;
                json_t *order;
                json_array_foreach(order_array, index, order) {
                    json_t *price = json_object_get(order, "price");
                    json_t *orderid = json_object_get(order, "client_order_id");
                    
                    char remain_field[64];
                    snprintf(remain_field, sizeof(remain_field), "remain_%s", coin_name);
                    json_t *remain = json_object_get(order, remain_field);
                    
                    const char *price_str = json_is_string(price) ? json_string_value(price) : "N/A";
                    const char *remain_str = json_is_string(remain) ? json_string_value(remain) : "N/A";
                    const char *orderid_str = json_is_string(orderid) ? json_string_value(orderid) : "N/A";
                    
                    printf("| %-10s | %-15s | %-17s | %-10s\t |\n", coin_name, price_str, remain_str, orderid_str);
                }
                free(coin_name);
            }
        } else if (json_is_array(orders)) {
            char *coin_name = coin_pair ? extract_coin_name(coin_pair) : strdup("N/A");
            
            size_t index;
            json_t *order;
            json_array_foreach(orders, index, order) {
                json_t *price = json_object_get(order, "price");
                json_t *orderid = json_object_get(order, "client_order_id");

                char remain_field[64];
                snprintf(remain_field, sizeof(remain_field), "remain_%s", coin_name);
                json_t *remain = json_object_get(order, remain_field);
                
                const char *price_str = json_is_string(price) ? json_string_value(price) : "N/A";
                const char *remain_str = json_is_string(remain) ? json_string_value(remain) : "N/A";
                const char *orderid_str = json_is_string(orderid) ? json_string_value(orderid) : "N/A";

                printf("| %-10s | %-15s | %-17s | %-10s\t |\n", coin_name, price_str, remain_str, orderid_str);
            }
            free(coin_name);
        } else {
            fprintf(stderr, "Unknown 'orders' format in JSON response\n");
        }
        
        printf("+------------+-----------------+-------------------+-----------------------------+\n");
    } else {
        json_t *error_field = json_object_get(root, "error");
        if (json_is_string(error_field)) {
            fprintf(stderr, "API Error: %s\n", json_string_value(error_field));
        } else {
            fprintf(stderr, "Unknown API error\n");
        }
    }
    
    json_decref(root);
}

void format_getinfo_table(const char *json_response) {
    json_error_t error;
    json_t *root = json_loads(json_response, 0, &error);
    if (!root) {
        fprintf(stderr, "JSON error: %s\n", error.text);
        return;
    }
    
    json_t *success = json_object_get(root, "success");
    if (!json_is_integer(success)) {
        fprintf(stderr, "Invalid success field\n");
        json_decref(root);
        return;
    }
    
    if (json_integer_value(success)) {
        json_t *return_obj = json_object_get(root, "return");
        if (!return_obj) {
            fprintf(stderr, "Missing 'return' object\n");
            json_decref(root);
            return;
        }
        
        json_t *balance = json_object_get(return_obj, "balance");
        json_t *balance_hold = json_object_get(return_obj, "balance_hold");
        
        if (!balance || !balance_hold) {
            fprintf(stderr, "Missing balance information\n");
            json_decref(root);
            return;
        }
        
        printf("+------------+-------------------+-------------------+\n");
        printf("| Asset      | Available Balance | On Hold Balance   |\n");
        printf("+------------+-------------------+-------------------+\n");
        
        const char *asset;
        json_t *value;
        int row_count = 0;
        
        json_object_foreach(balance, asset, value) {
            double available = json_value_to_double(value);
            double hold = 0.0;
            
            json_t *hold_value = json_object_get(balance_hold, asset);
            if (hold_value) {
                hold = json_value_to_double(hold_value);
            }
            
            if (available > 0 || hold > 0) {
                char avail_str[32], hold_str[32];
                snprintf(avail_str, sizeof(avail_str), "%.8f", available);
                snprintf(hold_str, sizeof(hold_str), "%.8f", hold);
                
                char *ptr;
                for (ptr = avail_str + strlen(avail_str) - 1; ptr > avail_str && *ptr == '0'; ptr--) *ptr = '\0';
                if (*ptr == '.') *ptr = '\0';
                for (ptr = hold_str + strlen(hold_str) - 1; ptr > hold_str && *ptr == '0'; ptr--) *ptr = '\0';
                if (*ptr == '.') *ptr = '\0';
                
                printf("| %-10s | %-17s | %-17s |\n", asset, avail_str, hold_str);
                row_count++;
            }
        }
        
        json_object_foreach(balance_hold, asset, value) {
            if (!json_object_get(balance, asset)) {
                double hold = json_value_to_double(value);
                if (hold > 0) {
                    printf("| %-10s | %-17s | %-17s |\n", asset, "0", "0");
                    row_count++;
                }
            }
        }
        
        if (row_count == 0) {
            printf("| No balances found with non-zero values |\n");
        }
        
        printf("+------------+-------------------+-------------------+\n");
    } else {
        json_t *error_field = json_object_get(root, "error");
        if (json_is_string(error_field)) {
            fprintf(stderr, "API Error: %s\n", json_string_value(error_field));
        } else {
            fprintf(stderr, "Unknown API error\n");
        }
    }
    
    json_decref(root);
}

void format_trade_response_table(const char *json_response, const char *coin, const char *price) {
    json_error_t error;
    json_t *root = json_loads(json_response, 0, &error);
    if (!root) {
        fprintf(stderr, "JSON error: %s\n", error.text);
        return;
    }
    
    json_t *success = json_object_get(root, "success");
    if (!json_is_integer(success)) {
        fprintf(stderr, "Invalid success field\n");
        json_decref(root);
        return;
    }
    
    if (json_integer_value(success)) {
        json_t *return_obj = json_object_get(root, "return");
        if (!return_obj) {
            fprintf(stderr, "Missing 'return' object\n");
            json_decref(root);
            return;
        }
        
        // Build dynamic field names
        char remain_field[64];
        snprintf(remain_field, sizeof(remain_field), "remain_%s", coin);
        
        json_t *remain_value = json_object_get(return_obj, remain_field);
        json_t *order_id = json_object_get(return_obj, "order_id");
        json_t *client_order_id = json_object_get(return_obj, "client_order_id");
        
        const char *remain_str = json_is_string(remain_value) ? json_string_value(remain_value) : "N/A";
        const char *order_id_str = json_is_integer(order_id) ? "N/A" : json_string_value(order_id);
        const char *client_order_id_str = json_is_string(client_order_id) ? json_string_value(client_order_id) : "N/A";
        
        // Handle integer order_id
        char order_id_buffer[32];
        if (json_is_integer(order_id)) {
            snprintf(order_id_buffer, sizeof(order_id_buffer), "%lld", json_integer_value(order_id));
            order_id_str = order_id_buffer;
        }
        
        printf("+------------+-----------------+-------------------+-----------------------------+\n");
        printf("| Coin Name  | Price           | Remaining Amount  | Client Order ID             |\n");
        printf("+------------+-----------------+-------------------+-----------------------------+\n");
        printf("| %-10s | %-15s | %-17s | %-27s |\n", coin, price, remain_str, client_order_id_str);
        printf("+------------+-----------------+-------------------+-----------------------------+\n");
    } else {
        json_t *error_field = json_object_get(root, "error");
        if (json_is_string(error_field)) {
            fprintf(stderr, "API Error: %s\n", json_string_value(error_field));
        } else {
            fprintf(stderr, "Unknown API error\n");
        }
    }
    
    json_decref(root);
}

void format_cancel_table(const char *json_response) {
    json_error_t error;
    json_t *root = json_loads(json_response, 0, &error);
    if (!root) {
        fprintf(stderr, "JSON error: %s\n", error.text);
        return;
    }
    
    json_t *success = json_object_get(root, "success");
    if (!json_is_integer(success)) {
        fprintf(stderr, "Invalid success field\n");
        json_decref(root);
        return;
    }
    
    if (json_integer_value(success)) {
        json_t *return_obj = json_object_get(root, "return");
        if (!return_obj) {
            fprintf(stderr, "Missing 'return' object\n");
            json_decref(root);
            return;
        }
        
        json_t *pair = json_object_get(return_obj, "pair");
        json_t *client_order_id = json_object_get(return_obj, "client_order_id");
        json_t *type = json_object_get(return_obj, "type");
        
        const char *pair_str = json_is_string(pair) ? json_string_value(pair) : "N/A";
        const char *client_order_id_str = json_is_string(client_order_id) ? json_string_value(client_order_id) : "N/A";
        const char *type_str = json_is_string(type) ? json_string_value(type) : "N/A";
        
        char *coin_name = extract_coin_name(pair_str);
        
        printf("+------------+------------+-----------------------------+------+\n");
        printf("| Coin Name  | Status     | Client Order ID             | Type |\n");
        printf("+------------+------------+-----------------------------+------+\n");
        printf("| %-10s | %-10s | %-27s | %-4s |\n", 
               coin_name, "Cancelled", client_order_id_str, type_str);
        printf("+------------+------------+-----------------------------+------+\n");
        
        free(coin_name);
    } else {
        json_t *error_field = json_object_get(root, "error");
        if (json_is_string(error_field)) {
            fprintf(stderr, "API Error: %s\n", json_string_value(error_field));
        } else {
            fprintf(stderr, "Unknown API error\n");
        }
    }
    
    json_decref(root);
}

int main(int argc, char *argv[]) {
    char *key = NULL;
    char *secret = NULL;
    
    if (!read_config(CONFIG_PATH, &key, &secret)) {
        return 1;
    }

    if (argc < 2) {
	p_head();
        fprintf(stderr, "Usage: \t%s <openallorder> or <open>\n", argv[0]);
        fprintf(stderr, "\t%s <openorder> <coin>\n", argv[0]);
        fprintf(stderr, "\t%s <buy> <coin> <coin_price> <spend_idr>\n", argv[0]);
        fprintf(stderr, "\t%s <sell> <coin> <coin_price> <quantity>\n", argv[0]);
        fprintf(stderr, "\t%s <cancel> <orderid>\n", argv[0]);
        fprintf(stderr, "\t%s getInfo\n", argv[0]);
        fprintf(stderr, "\t%s about\n", argv[0]);
        free(key);
        free(secret);
        return 1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl init failed\n");
        free(key);
        free(secret);
        return 1;
    }

    time_t t = time(NULL);
    long epoch_ms = t * 1000;
    long recv_window = epoch_ms + 49900000;

    char postdata[MAX_PAYLOAD] = {0};
    char client_order_id[128] = {0};
    const char *command = argv[1];
    int format_table = 0;
    const char *coin_pair_arg = NULL;
    int is_getinfo = 0;
    int is_trade = 0;
    int is_cancel = 0;
    const char *trade_coin = NULL;
    const char *trade_price = NULL;


    if ((strcmp(command, "openallorder") == 0) || (strcmp(command, "open") == 0)) {
        p_head();
        format_table = 1;
        snprintf(postdata, sizeof(postdata),
                 "method=openOrders&timestamp=%ld&recvWindow=%ld",
                 epoch_ms, recv_window);
    } else if (strcmp(command, "openorder") == 0 && argc >= 3) {
        p_head();
        format_table = 1;
        coin_pair_arg = argv[2];
        snprintf(postdata, sizeof(postdata),
                 "method=openOrders&timestamp=%ld&recvWindow=%ld&pair=%s_idr",
                 epoch_ms, recv_window, coin_pair_arg);
    } else if (strcmp(command, "buy") == 0 && argc >= 5) {
        p_head();
        is_trade = 1;
        trade_coin = argv[2];
        trade_price = argv[3];
        snprintf(client_order_id, sizeof(client_order_id), "%sidr-%ld-idX", argv[2], t);
        snprintf(postdata, sizeof(postdata),
                 "method=trade&timestamp=%ld&recvWindow=%ld&pair=%s_idr&type=buy&price=%s&idr=%s&client_order_id=%s",
                 epoch_ms, recv_window, argv[2], argv[3], argv[4], client_order_id);
    } else if (strcmp(command, "sell") == 0 && argc >= 5) {
        p_head();
        is_trade = 1;
        trade_coin = argv[2];
        trade_price = argv[3];
        snprintf(client_order_id, sizeof(client_order_id), "%sidr-%ld-idX", argv[2], t);
        snprintf(postdata, sizeof(postdata),
                 "method=trade&timestamp=%ld&recvWindow=%ld&pair=%s_idr&type=sell&price=%s&idr=%s&client_order_id=%s&%s=%s",
                 epoch_ms, recv_window, argv[2], argv[3], argv[4], client_order_id, argv[2], argv[4]);
    } else if (strcmp(command, "cancel") == 0 && argc >= 3) { 
        p_head();
        is_cancel = 1;
        snprintf(postdata, sizeof(postdata),
                 "method=cancelByClientOrderId&timestamp=%ld&recvWindow=%ld&client_order_id=%s",
                 epoch_ms, recv_window, argv[2]);
    } else if ( (strcmp(command, "getinfo") == 0) || (strcmp(command, "getInfo") == 0) ) {
        p_head();
        format_table = 1;
        is_getinfo = 1;
        snprintf(postdata, sizeof(postdata),
                 "method=getInfo&timestamp=%ld&recvWindow=%ld",
                 epoch_ms, recv_window); 
    } else if (strcmp(command, "about") == 0) {
        p_head();
	printf("This program uses the Indodax REST API, a proof of concept (POC) demonstrating that we can create and utilize a REST API with C.\n\nIf you\'d like to give a gift, please send some DOGE to my wallet \"D6ckQMfcWSosY7J4rNQkY1rKX1pQTmNuTt\"\n\nOr if you\'re an Indodax user, you can send it using my username \"idban\" without the quotation marks.\n\n");
	return 1;
    } else {
	p_head();
        fprintf(stderr, "Invalid or insufficient arguments\n");
        free(key);
        free(secret);
        curl_easy_cleanup(curl);
        return 1;
    }

    char signature[129] = {0};
    hmac_sha512(postdata, secret, signature);

    struct curl_slist *headers = NULL;
    char key_hdr[MAX_HEADER], sign_hdr[MAX_HEADER];
    snprintf(key_hdr, sizeof(key_hdr), "Key: %s", key);
    snprintf(sign_hdr, sizeof(sign_hdr), "Sign: %s", signature);
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, key_hdr);
    headers = curl_slist_append(headers, sign_hdr);

    curl_easy_setopt(curl, CURLOPT_URL, "https://indodax.com/tapi");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);
    chunk.size = 0;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "\nCURL error: %s\n", curl_easy_strerror(res));
    } else {
        if (is_trade) {
            format_trade_response_table(chunk.memory, trade_coin, trade_price);
        } else if (format_table) {
            if (is_getinfo) {
                format_getinfo_table(chunk.memory);
            } else {
                format_orders_table(chunk.memory, coin_pair_arg);
            }
        } else if (is_cancel) { 
            format_cancel_table(chunk.memory);
        } else {
            printf("%s\n", chunk.memory);
        }
    }

    free(chunk.memory);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(key);
    free(secret);
    return 0;
}
