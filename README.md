An HTTP client written in C used to interact with a REST API of a library
database. It can send requests to the server and receive responses to perform
various operations: registering, logging in, logging out, getting a list
of all books in the library, getting a book by id, adding a book, deleting a
book.
The exposed API is the following (just type one of the following in the
terminal):

* register: create a new account
* login
* logout
* enter_library: receive authentication token
* get_books: prints user books
* get_book: get information about a specific book
* add_book: add a book to the library
* delete_book: delete a book from the library
