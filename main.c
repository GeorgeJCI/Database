/* Making a database in C - George Carlyle-Ive (29/10/2023) */
/* following a tutorial as an programming exercise*/
/* Link to tutorial: https://cstack.github.io/db_tutorial/ */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

/* IMPORTED FUNCTION FOR A WINDOWS 'getline()' FUNCTION */

/* The original code is public domain -- Will Hartung 4/9/09 */
/* Modifications, public domain as well, by Antti Haapala, 11/10/17
   - Switched to getc on 5/23/19 */

// if typedef doesn't exist (msvc, blah)
typedef intptr_t ssize_t;

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    size_t pos;
    int c;

    if (lineptr == NULL || stream == NULL || n == NULL) {
        errno = EINVAL;
        return -1;
    }

    c = getc(stream);
    if (c == EOF) {
        return -1;
    }

    if (*lineptr == NULL) {
        *lineptr = malloc(128);
        if (*lineptr == NULL) {
            return -1;
        }
        *n = 128;
    }

    pos = 0;
    while(c != EOF) {
        if (pos + 1 >= *n) {
            size_t new_size = *n + (*n >> 2);
            if (new_size < 128) {
                new_size = 128;
            }
            char *new_ptr = realloc(*lineptr, new_size);
            if (new_ptr == NULL) {
                return -1;
            }
            *n = new_size;
            *lineptr = new_ptr;
        }

        ((unsigned char *)(*lineptr))[pos ++] = c;
        if (c == '\n') {
            break;
        }
        c = getc(stream);
    }

    (*lineptr)[pos] = '\0';
    return pos;
}
/* END OF IMPORT */




/* makes a inputbuffer for reading to the terminal */
typedef struct{
    char* buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNISED_COMMAND
} MetaCommandResult;

typedef enum {PREPARE_SUCCESS, PREPARE_SYNTAX_ERROR, PREPARE_UNRECOGNISED_STATEMENT} PrepareResult;

typedef enum {STATEMENT_INSERT, STATEMENT_SELECT} StatementType;

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
typedef struct{
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE];
    char email[COLUMN_EMAIL_SIZE];
} Row;

typedef struct{
    StatementType type;
    Row row_to_insert; /* Only used by insert statement*/
} Statement;

/* define the compact representation of a row */
#define size_of_attribute(Struct,Attribute) sizeof(((Struct*)0)->Attribute)
const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

/* Covert to and from the compact representation */
void serialize_row(Row* source, void* destination){
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}
void deserialize_row(void* source, Row* destination){
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

/* structure that points to pages of rows and tracks how many there are */
const uint32_t PAGE_SIZE = 4096;
#define TABLE_MAX_PAGES 100
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

typedef struct{
    uint32_t num_rows;
    void* pages[TABLE_MAX_PAGES];
} Table;

/* Locate where to read/write in memory for a particular row */
void* row_slot(Table* table, uint32_t row_num){
    uint32_t page_num = row_num / ROWS_PER_PAGE;
    void* page = table->pages[page_num];
    if(page == NULL){
        /* Allocate memory only when we try to access page */
        page = table->pages[page_num] = malloc(PAGE_SIZE);
    }
    uint32_t row_offset = row_num % ROWS_PER_PAGE;
    uint32_t byte_offset = row_offset * ROW_SIZE;
    return page + byte_offset;
}

InputBuffer* new_input_buffer(){
    InputBuffer* input_buffer = (InputBuffer*)malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}
/* Input prompt while terminal is active */
void print_prompt(){
    printf("db > ");
}

/* function to read from keyboard */
void read_input(InputBuffer* input_buffer){
    ssize_t bytes_read = 
        getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

    if(bytes_read <= 0){
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }

    input_buffer->input_length = bytes_read - 1;
    input_buffer->buffer[bytes_read - 1] = 0;
}

void close_input_buffer(InputBuffer* input_buffer){
    free(input_buffer->buffer);
    free(input_buffer);
}

/* Meta-commands are non-SQL statements they all start with a dot
   so we check and handle them in a diffrent function */

MetaCommandResult do_meta_command(InputBuffer* input_buffer){
    if(strcmp(input_buffer->buffer, ".exit") == 0){
        exit(EXIT_SUCCESS);
    } else{
        return META_COMMAND_UNRECOGNISED_COMMAND;
    }
}
/* prepare_statement allows to select diffrent functions */
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement){

if(strncmp(input_buffer->buffer, "insert", 6) == 0){
    statement->type = STATEMENT_INSERT;
    int args_assigned = sscanf(
        input_buffer->buffer, "insert %d %s %s", &(statement->row_to_insert.id),
        statement->row_to_insert.username, statement->row_to_insert.email);
    if(args_assigned < 3){
        return PREPARE_SYNTAX_ERROR;
    }
    return PREPARE_SUCCESS;
}
if(strcmp(input_buffer->buffer, "select") == 0){
    statement->type = STATEMENT_SELECT;
    return PREPARE_SUCCESS;
}

return PREPARE_UNRECOGNISED_STATEMENT;
}

void execute_statement(Statement* statement){
    switch(statement->type){
        case(STATEMENT_INSERT):
            printf("This is where we would do an insert.\n");
            break;
        case(STATEMENT_SELECT):
            printf("This is where we would do a select.\n");
            break;
    }
}

/* main function of the program */
int main(int argc, char* argv[]){

    /* REPL - read, evaluate, print, loop */
    InputBuffer* input_buffer = new_input_buffer();
    while(true){
        print_prompt();
        read_input(input_buffer);

            if(input_buffer->buffer[0] == '.'){
                switch(do_meta_command(input_buffer)){
                    case (META_COMMAND_SUCCESS):
                        continue;
                    case(META_COMMAND_UNRECOGNISED_COMMAND):
                        printf("Unrecognised command '%s'\n", input_buffer->buffer);
                        continue;
                }
            }

            Statement statement;
            switch(prepare_statement(input_buffer, &statement)){
                case(PREPARE_SUCCESS):
                break;
                case(PREPARE_UNRECOGNISED_STATEMENT):
                printf("Unrecognised keyword at the start of '%s'.\n",
                        input_buffer->buffer);
                continue;
            }

            execute_statement(&statement);
            printf("Executed.\n");
        
    }
}