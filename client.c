#include <stdio.h>      /* printf, sprintf */
#include <stdlib.h>     /* exit, atoi, malloc, free */
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */
#include <arpa/inet.h>
#include "helpers.h"
#include "requests.h"
#include "parson.h"
#include "client.h"

/* return an array of strings representing the lines of the input string
 * NOTE: the caller is responsible for freeing the returned result
 */
char **split_string_into_lines(char *string, int *line_n)
{
    // we'll tokenize by newlines
    char *token = strtok(string, "\n");
    int i = 0;
    char **lines = calloc(100, sizeof(char *));
    if (lines == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    // and add the tokens to our result
    while (token != NULL) {
        lines[i] = token;
        i++;
        token = strtok(NULL, "\n");
    }

    *line_n = i;

    return lines;
}

/* ask for a username and a password and return a formatted
 * JSON string containing that information
 * NOTE: the caller is responsible for freeing the returned string
 */
char *user_pass_prompt()
{
    char user_input_buffer[BUFLEN];
    char username[BUFLEN];
    char password[BUFLEN];

    printf("username=");
    fgets(user_input_buffer, BUFLEN, stdin);
    user_input_buffer[strlen(user_input_buffer) - 1] = '\0';
    strcpy(username, user_input_buffer);
    if (strchr(username, ' ') != NULL) {
        printf("Username cannot contain spaces!\n");
        return NULL;
    }

    printf("password=");
    fgets(user_input_buffer, BUFLEN, stdin);
    user_input_buffer[strlen(user_input_buffer) - 1] = '\0';
    strcpy(password, user_input_buffer);
    if (strchr(password, ' ') != NULL) {
        printf("Password cannot contain spaces!\n");
        return NULL;
    }

    // generate JSON containing username and password
    JSON_Value *root = json_value_init_object();
    JSON_Object *root_obj = json_value_get_object(root);

    json_object_set_string(root_obj, "username", username);
    json_object_set_string(root_obj, "password", password);

    char *JSON_raw = json_serialize_to_string_pretty(root);

    json_value_free(root);

    return JSON_raw;
}

// represents the "register" command
void register_user()
{
    char *message;
    char *response;
    int sockfd;

    // get username and password from user and generate JSON
    char *JSON_raw = user_pass_prompt();
    if (JSON_raw == NULL)
        return;

    // split JSON into lines to make it easier to use compute_post_request
    int JSON_raw_line_n;
    char **JSON_raw_lines = split_string_into_lines(JSON_raw,
                                                &JSON_raw_line_n);

    // generate raw HTTP post request
    message = compute_post_request(SERVER_IP,
                "/api/v1/tema/auth/register", "application/json",
                JSON_raw_lines, JSON_raw_line_n, NULL, 0);

    // make the HTTP request
    sockfd = open_connection(SERVER_IP, HTTP_PORT, AF_INET, SOCK_STREAM, 0);

    send_to_server(sockfd, message);
    response = receive_from_server(sockfd);

    close_connection(sockfd);

    // check response
    char *json_response = basic_extract_json_response(response);

    // if there's no JSON, the request was successful, otherwise print error
    if (json_response == NULL)
        printf("201 - OK - Successfully registered\n");
    else {
        JSON_Value *json_response_value = json_parse_string(json_response);
        JSON_Object *json_response_object = json_value_get_object(json_response_value);
        const char *error = json_object_get_string(json_response_object, "error");

        if (error)
            printf("400 - Bad Request - %s\n", error);

        json_value_free(json_response_value);
    }

    // free memory
    json_free_serialized_string(JSON_raw);
    free(JSON_raw_lines);
    free(message);
    free(response);
}

// extracts cookie from an HTTP response
char *get_cookie(char *response)
{
    int response_line_n;
    char **response_lines = split_string_into_lines(response,
                                                &response_line_n);
    char *cookie = NULL;

    for (int i = 0; i < response_line_n; i++)
        if (strstr(response_lines[i], "Set-Cookie") != NULL) {
            char *token = strtok(response_lines[i], " ;");

            while (token != NULL) {
                if (strstr(token, "connect.sid") != NULL) {
                    cookie = calloc(strlen(token) + 1, sizeof(char));
                    if (cookie == NULL) {
                        perror("calloc");
                        exit(EXIT_FAILURE);
                    }


                    strcpy(cookie, token);
                    break;
                }

                token = strtok(NULL, " ;");
            }

            break;
        }

    // free memory
    free(response_lines);

    return cookie;
}

// represents the "login" command
char *login()
{
    char *message;
    char *response;
    char *cookie = NULL;
    int sockfd;

    // get username and password from user and generate JSON
    char *JSON_raw = user_pass_prompt();

    // split JSON into lines to make it easier to use compute_post_request
    int JSON_raw_line_n;
    char **JSON_raw_lines = split_string_into_lines(JSON_raw,
                                                &JSON_raw_line_n);

    // generate the raw text http request
    message = compute_post_request(SERVER_IP, "/api/v1/tema/auth/login",
                "application/json", JSON_raw_lines, JSON_raw_line_n, NULL, 0);


    // make the HTTP request
    sockfd = open_connection(SERVER_IP, HTTP_PORT, AF_INET, SOCK_STREAM, 0);

    send_to_server(sockfd, message);
    response = receive_from_server(sockfd);

    close_connection(sockfd);

    char *json_response = basic_extract_json_response(response);

    // check for errors based on whether we received JSON or not
    if (json_response == NULL) {
        printf("200 - OK - Successfully logged in\n");
        cookie = get_cookie(response);
    } else {
        JSON_Value *json_response_value = json_parse_string(json_response);
        JSON_Object *json_response_object = json_value_get_object(json_response_value);
        const char *error = json_object_get_string(json_response_object, "error");

        if (error)
            printf("400 - Bad Request - %s\n", error);

        json_value_free(json_response_value);
    }

    // free memory
    json_free_serialized_string(JSON_raw);
    free(JSON_raw_lines);
    free(message);
    free(response);

    return cookie;
}

// represents the "enter_library" command
char *enter_library(char **cookies, int cookies_n)
{
    char *auth_token = NULL;
    char *message;
    char *response;
    int sockfd;

    // generate the raw text http GET request
    message = compute_get_request(SERVER_IP, "/api/v1/tema/library/access",
                NULL, cookies, cookies_n);

    // make the HTTP request
    sockfd = open_connection(SERVER_IP, HTTP_PORT, AF_INET, SOCK_STREAM, 0);

    send_to_server(sockfd, message);
    response = receive_from_server(sockfd);

    close_connection(sockfd);

    char *json_response = basic_extract_json_response(response);

    JSON_Value *json_response_value = json_parse_string(json_response);
    JSON_Object *json_response_object = json_value_get_object(json_response_value);

    // try to extract the authentication token
    const char *result = json_object_get_string(json_response_object, "token");

    // if there's no token, print error, otherwise save it
    if (result == NULL) {
        const char *error = json_object_get_string(json_response_object, "error");
        if (error)
            printf("400 - Bad Request - %s\n", error);
    } else {
        printf("200 - OK - Successfully entered library\n");
        auth_token = calloc(strlen(result) + 1, sizeof(char));
        strcpy(auth_token, result);
    }

    // free memory
    json_value_free(json_response_value);
    free(message);
    free(response);

    return auth_token;
}

// represents the "get_books" command
void get_books(char **cookies, int cookies_n, char *auth_token)
{
    char *message;
    char *response;
    int sockfd;

    // generate the raw text http GET request with authentication
    message = compute_get_request_auth(SERVER_IP, "/api/v1/tema/library/books",
                NULL, cookies, cookies_n, auth_token);

    // make the HTTP request
    sockfd = open_connection(SERVER_IP, HTTP_PORT, AF_INET, SOCK_STREAM, 0);

    send_to_server(sockfd, message);
    response = receive_from_server(sockfd);

    close_connection(sockfd);

    char *json_response = basic_extract_json_response(response);

    // parse book array
    JSON_Value *json_response_value = json_parse_string(json_response);
    JSON_Array *json_response_array = json_value_get_array(json_response_value);

    if (json_response_array == NULL) {
        JSON_Object *json_response_object = json_value_get_object(json_response_value);
        const char *error = json_object_get_string(json_response_object, "error");

        printf("400 - Bad Request - %s\n", error);

        json_value_free(json_response_value);
        free(message);
        free(response);
        return;
    } else {
        printf("200 - OK - Successfully retrieved books\n");
    }

    int n = json_array_get_count(json_response_array);

    // print each book
    for (int i = 0; i < n; i++) {
        JSON_Object *json_object = json_array_get_object(json_response_array, i);
        long int id = (long int) json_object_get_number(json_object, "id");
        const char *title = json_object_get_string(json_object, "title");

        printf("%ld: %s\n", id, title);
    }

    // free memory
    json_value_free(json_response_value);
    free(message);
    free(response);
}

/* asks the user for a book id and the generates the url
 * NOTE: the caller is responsible for freeing the returned string
 */
char *id_prompt()
{
    char id[BUFLEN];

    printf("id=");
    fgets(id, BUFLEN, stdin);
    id[strlen(id) - 1] = '\0';

    if (strlen(id) == 0) {
        printf("You have to enter an ID, try again!\n");
        return NULL;
    }

    char *url = calloc(strlen("/api/v1/tema/library/books/") + strlen(id) + 1, sizeof(char));
    if (url == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    sprintf(url, "/api/v1/tema/library/books/%s", id);

    return url;
}

// represents the "get_book" command
void get_book(char **cookies, int cookies_n, char *auth_token)
{
    char *message;
    char *response;
    int sockfd;

    // get book id and generate url
    char *url = id_prompt();

    // generate the raw text http GET request with authentication
    message = compute_get_request_auth(SERVER_IP, url, NULL, cookies,
                                        cookies_n, auth_token);

    // make the HTTP request
    sockfd = open_connection(SERVER_IP, HTTP_PORT, AF_INET, SOCK_STREAM, 0);

    send_to_server(sockfd, message);
    response = receive_from_server(sockfd);

    close_connection(sockfd);

    char *json_response = basic_extract_json_response(response);
    JSON_Value *json_response_value = json_parse_string(json_response);
    JSON_Object *json_response_object = json_value_get_object(json_response_value);

    const char *error = json_object_get_string(json_response_object, "error");

    // if there's an error, print it, otherwise print the book
    if (error != NULL) {
        printf("404 - Not Found - %s\n", error);
    } else {
        printf("200 - OK - Successfully retrieved book\n");

        long int id = (long int) json_object_get_number(json_response_object, "id");
        const char *title = json_object_get_string(json_response_object, "title");
        const char *author = json_object_get_string(json_response_object, "author");
        const char *publisher = json_object_get_string(json_response_object, "publisher");
        const char *genre = json_object_get_string(json_response_object, "genre");
        long int page_count = (long int) json_object_get_number(json_response_object, "page_count");

        printf("ID: %ld\n", id);
        printf("Title: %s\n", title);
        printf("Author: %s\n", author);
        printf("Publisher: %s\n", publisher);
        printf("Genre: %s\n", genre);
        printf("Page count: %ld\n", page_count);
    }

    // free memory
    json_value_free(json_response_value);
    free(message);
    free(response);
    free(url);
}

// represents the "delete_book" command
void add_book(char **cookies, int cookies_n, char *auth_token)
{
    char *message;
    char *response;
    int sockfd;
    char title[BUFLEN];
    char author[BUFLEN];
    char genre[BUFLEN];
    char publisher[BUFLEN];
    char page_count_string[BUFLEN];
    double page_count;

    // get book info from user
    printf("title=");
    fgets(title, BUFLEN, stdin);
    title[strlen(title) - 1] = '\0';

    printf("author=");
    fgets(author, BUFLEN, stdin);
    author[strlen(author) - 1] = '\0';

    printf("genre=");
    fgets(genre, BUFLEN, stdin);
    genre[strlen(genre) - 1] = '\0';

    printf("publisher=");
    fgets(publisher, BUFLEN, stdin);
    publisher[strlen(publisher) - 1] = '\0';

    printf("page_count=");
    fgets(page_count_string, BUFLEN, stdin);
    page_count_string[strlen(page_count_string) - 1] = '\0';

    // validate page count
    for (int i = 0; i < strlen(page_count_string); i++) {
        if (page_count_string[i] < '0' || page_count_string[i] > '9') {
            printf("Page count must be a number!\n");
            return;
        }
    }

    page_count = atof(page_count_string);

    // generate the JSON for the book
    JSON_Value *root = json_value_init_object();
    JSON_Object *root_obj = json_value_get_object(root);

    json_object_set_string(root_obj, "title", title);
    json_object_set_string(root_obj, "author", author);
    json_object_set_string(root_obj, "genre", genre);
    json_object_set_string(root_obj, "publisher", publisher);
    json_object_set_number(root_obj, "page_count", page_count);

    char *JSON_raw = json_serialize_to_string_pretty(root);
    int JSON_raw_line_n;
    char **JSON_raw_lines = split_string_into_lines(JSON_raw,
                                                &JSON_raw_line_n);

    // generate the raw text http POST request with authentication
    message = compute_post_request_auth(SERVER_IP, "/api/v1/tema/library/books",
                                        "application/json", JSON_raw_lines,
                                        JSON_raw_line_n, cookies, cookies_n,
                                        auth_token);

    // make the HTTP request
    sockfd = open_connection(SERVER_IP, HTTP_PORT, AF_INET, SOCK_STREAM, 0);

    send_to_server(sockfd, message);
    response = receive_from_server(sockfd);

    close_connection(sockfd);

    char *json_response = basic_extract_json_response(response);

    // prin success or error
    if (json_response == NULL) {
        printf("200 - OK - Successfully added book\n");
    } else {
        JSON_Value *json_response_value = json_parse_string(json_response);
        JSON_Object *json_response_object = json_value_get_object(json_response_value);
        const char *error = json_object_get_string(json_response_object, "error");

        printf("400 - Bad Request - %s\n", error);

        json_value_free(json_response_value);
    }

    // free memory
    json_value_free(root);
    json_free_serialized_string(JSON_raw);
    free(message);
    free(response);
    free(JSON_raw_lines);
}

// represents the "delete_book" command
void delete_book(char **cookies, int cookies_n, char *auth_token)
{
    char *message;
    char *response;
    int sockfd;

    // get the id of the book from the user and generate the url
    char *url = id_prompt();

    // generate the raw text http DELETE request with authentication
    message = compute_delete_request_auth(SERVER_IP, url, NULL, cookies,
                                          cookies_n, auth_token);

    // make the HTTP request
    sockfd = open_connection(SERVER_IP, HTTP_PORT, AF_INET, SOCK_STREAM, 0);

    send_to_server(sockfd, message);
    response = receive_from_server(sockfd);

    close_connection(sockfd);

    char *json_response = basic_extract_json_response(response);

    // print success or error
    if (json_response == NULL) {
        printf("200 - OK - Successfully deleted book\n");
    } else {
        JSON_Value *json_response_value = json_parse_string(json_response);
        JSON_Object *json_response_object = json_value_get_object(json_response_value);
        const char *error = json_object_get_string(json_response_object, "error");

        printf("404 - Not Found - %s\n", error);

        json_value_free(json_response_value);
    }

    // free memory
    free(url);
    free(message);
    free(response);
}

// represents the "logout" command
void logout(char **cookies, int cookies_n)
{
    char *message;
    char *response;
    int sockfd;

    // generate the raw text http GET request
    message = compute_get_request(SERVER_IP, "/api/v1/tema/auth/logout",
                                       NULL, cookies, cookies_n);

    // make the HTTP request
    sockfd = open_connection(SERVER_IP, HTTP_PORT, AF_INET, SOCK_STREAM, 0);

    send_to_server(sockfd, message);
    response = receive_from_server(sockfd);

    close_connection(sockfd);

    char *json_response = basic_extract_json_response(response);

    // print success or error
    if (json_response == NULL) {
        printf("200 - OK - Successfully logged out\n");
    } else {
        JSON_Value *json_response_value = json_parse_string(json_response);
        JSON_Object *json_response_object = json_value_get_object(json_response_value);
        const char *error = json_object_get_string(json_response_object, "error");

        printf("400 - Bad Request - %s\n", error);

        json_value_free(json_response_value);
    }

    // free memory
    free(message);
    free(response);
}

int main(void)
{
    char user_input_buffer[BUFLEN];
    char *auth_token = NULL;
    int cookies_n = 0;
    int cookies_cap = 100;
    char **cookies = calloc(cookies_cap, sizeof(char *));
    if (cookies == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    // receive commands from stdin until the user sends "exit"
    while (1) {
        fgets(user_input_buffer, BUFLEN, stdin);

        if (strcmp(user_input_buffer, "register\n") == 0) {
            register_user();
        }
        else if(strcmp(user_input_buffer, "login\n") == 0) {
            char *cookie = login();

            if (cookie == NULL)
                continue;

            // reallocate memory if necessary
            if (cookies_n == cookies_cap) {
                cookies_cap *= 2;
                cookies = realloc(cookies, cookies_cap * sizeof(char *));
                if (cookies == NULL) {
                    perror("realloc");
                    exit(EXIT_FAILURE);
                }
            }

            // add cookie to cookie list
            cookies[cookies_n++] = cookie;
        }
        else if (strcmp(user_input_buffer, "enter_library\n") == 0) {
            /* check if user is logged in and don't bother sending the request
             * if not since we know it would result in an error anyway
             */
            if (cookies_n == 0) {
                printf("You are not logged in!\n");
                continue;
            }

            auth_token = enter_library(cookies, cookies_n);
        }
        else if (strcmp(user_input_buffer, "get_books\n") == 0) {
            /* check if user has an auth token and don't bother sending the
             * request if not since we know it would result in an error anyway
             */
            if (auth_token == NULL) {
                printf("You don't have an authentication token! Hint: enter_library\n");
                continue;
            }

            get_books(cookies, cookies_n, auth_token);
        }
        else if (strcmp(user_input_buffer, "get_book\n") == 0) {
            if (auth_token == NULL) {
                printf("You don't have an authentication token! Hint: enter_library\n");
                continue;
            }

            get_book(cookies, cookies_n, auth_token);
        }
        else if (strcmp(user_input_buffer, "add_book\n") == 0) {
            if (auth_token == NULL) {
                printf("You don't have an authentication token! Hint: enter_library\n");
                continue;
            }

            add_book(cookies, cookies_n, auth_token);
        }
        else if (strcmp(user_input_buffer, "delete_book\n") == 0) {
            if (auth_token == NULL) {
                printf("You don't have an authentication token! Hint: enter_library\n");
                continue;
            }

            delete_book(cookies, cookies_n, auth_token);
        }
        else if (strcmp(user_input_buffer, "logout\n") == 0) {
            if (cookies_n == 0) {
                printf("You are not logged in!\n");
                continue;
            }

            logout(cookies, cookies_n);

            // remove session info like auth token and cookies
            if (auth_token != NULL)
                free(auth_token);
            auth_token = NULL;
            for (int i = 0; i < cookies_n; i++) {
                free(cookies[i]);
                cookies[i] = NULL;
            }
            cookies_n = 0;
        }
        else if (strcmp(user_input_buffer, "exit\n") == 0) {
            break;
        }
        else {
            printf("Invalid input\n");
        }
    }

    // free memory
    for (int i = 0; i < cookies_n; i++)
        free(cookies[i]);
    free(cookies);
    if (auth_token != NULL)
        free(auth_token);

    return 0;
}
