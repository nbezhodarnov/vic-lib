#include "third_party/clex/clex.h"
#include "third_party/cc/cc.h"
#include "lib/vic.h"

#include <string.h>

#include <stdio.h>
#include <ctype.h>

typedef enum
{
  INT = UNDEFINED + 1,
  OPARAN,
  CPARAN,
  OSQUAREBRACE,
  CSQUAREBRACE,
  OCURLYBRACE,
  CCURLYBRACE,
  COMMA,
  CHAR,
  STAR,
  RETURN,
  SEMICOL,
  CONSTANT,
  EQUAL,
  QUOTE,
  DOUBLE_QUOTE,
  IDENTIFIER,
} token_kind;

typedef enum
{
  MAIN_START = 0x00,
  MAIN_IN = 0x01,
  MAIN_FINISH = 0x02,
  MAIN_CAPTURE_NAME = 0x04,
  MAIN_CAPTURE_OPARAN = 0x08,
  MAIN_CAPTURE_CPARAN = 0x10,
  MAIN_CAPTURE_OCURLYBRACE = 0x20,
} main_parser_state;
typedef enum
{
  VIC_START = 0x00,
  VIC_FINISH = 0x01,
  VIC_CAPTURE_NAME = 0x02,
  VIC_CAPTURE_EQUAL = 0x04,
  VIC_CAPTURE_CREATION = 0x08,
  VIC_CAPTURE_ABSTRACTION = 0x10,
  VIC_CAPTURE_ROUTINE = 0x20,
} vic_parser_state;

typedef struct
{
  int abstraction;
  char *routine_name;
} vic_lexer_data;

int main(int argc, char **argv)
{
  if (argc < 3)
  {
    printf("Usage: <input_file> <output_dir>\n");
    return -1;
  }

  char *input_file = argv[1];
  char *output_dir = argv[2];

  clexLexer *lexer = clexInit();

  clexRegisterKind(lexer, "int", INT);
  clexRegisterKind(lexer, "\\(", OPARAN);
  clexRegisterKind(lexer, "\\)", CPARAN);
  clexRegisterKind(lexer, "\\[|<:", OSQUAREBRACE);
  clexRegisterKind(lexer, "\\]|:>", CSQUAREBRACE);
  clexRegisterKind(lexer, "{|<%", OCURLYBRACE);
  clexRegisterKind(lexer, "}|%>", CCURLYBRACE);
  clexRegisterKind(lexer, ",", COMMA);
  // clexRegisterKind(lexer, "char", CHAR);
  clexRegisterKind(lexer, "\\*", STAR);
  // clexRegisterKind(lexer, "return", RETURN);
  // clexRegisterKind(lexer, "[1-9][0-9]*([uU])?([lL])?([lL])?", CONSTANT);
  // clexRegisterKind(lexer, "[1-9][0-9]*([uU])?([lL])?([lL])?", CONSTANT);
  clexRegisterKind(lexer, ";", SEMICOL);
  clexRegisterKind(lexer, "=", EQUAL);
  // clexRegisterKind(lexer, "\"([^\"\\]|\\.)*\"", STRINGLITERAL);
  clexRegisterKind(lexer, "'", QUOTE);
  clexRegisterKind(lexer, "\"", DOUBLE_QUOTE);
  clexRegisterKind(lexer, "[a-zA-Z_]([a-zA-Z_]|[0-9])*", IDENTIFIER);

  cc_map(char *, vic_lexer_data) vic_map;
  cc_init(&vic_map);

  FILE *ifile;
  char *buffer;
  long file_size;

  // Open the file in read mode
  ifile = fopen(input_file, "r");
  if (ifile == NULL)
  {
    perror("Error opening file");
    return -1;
  }

  // Find the size of the file
  fseek(ifile, 0, SEEK_END);
  file_size = ftell(ifile);
  rewind(ifile); // Go back to the start of the file

  // Allocate memory for the entire content
  buffer = (char *)calloc(file_size + 1, sizeof(char));
  if (buffer == NULL)
  {
    perror("Error allocating memory");
    fclose(ifile);
    return -1;
  }

  // Read the file into the buffer
  fread(buffer, 1, file_size, ifile);
  
  // Clean up
  fclose(ifile);

  clexReset(lexer, buffer);

  buffer = (char *)calloc(file_size + 1, sizeof(char));
  if (buffer == NULL)
  {
    perror("Error allocating memory");
    fclose(ifile);
    return -1;
  }

  main_parser_state main_state = MAIN_START;
  int main_paran_count = 0;

  char *current_vic_name = NULL;
  int current_vic_abstraction = 0;

  vic_lexer_data dummy_vic_data = {.abstraction = 0, .routine_name = NULL};
  vic_parser_state vic_state = VIC_START;

  clexToken token = clex(lexer);
  while (token.kind != EOF)
  {
    if (token.kind == INT && main_state == MAIN_START)
    {
      char main_buffer[256] = {};
      main_state = MAIN_CAPTURE_NAME;

      do
      {
        strcat(main_buffer, token.lexeme);
        token = clex(lexer);

        switch (token.kind)
        {
        case IDENTIFIER:
          if (strcmp(token.lexeme, "main") == 0)
          {
            main_state = MAIN_CAPTURE_OPARAN;
          }
          else if (main_state != MAIN_CAPTURE_CPARAN)
          {
            // probably not a main function
            main_state = MAIN_START;
          }
          break;
        case OPARAN:
          if (main_state == MAIN_CAPTURE_OPARAN)
          {
            main_state = MAIN_CAPTURE_CPARAN;
          }
          break;
        case CPARAN:
          if (main_state == MAIN_CAPTURE_CPARAN)
          {
            main_state = MAIN_CAPTURE_OCURLYBRACE;
          }
          break;
        case OCURLYBRACE:
          if (main_state == MAIN_CAPTURE_OCURLYBRACE)
          {
            main_state = MAIN_IN;
            main_paran_count = 1;
          }
          break;
        case INT:
          if (main_state != MAIN_CAPTURE_CPARAN)
          {
            // probably not a main function
            main_state = MAIN_START;
          }
          break;
        case COMMA:
          if (main_state != MAIN_CAPTURE_CPARAN)
          {
            // probably not a main function
            main_state = MAIN_START;
          }
          break;
        case STAR:
          if (main_state != MAIN_CAPTURE_CPARAN)
          {
            // probably not a main function
            main_state = MAIN_START;
          }
          break;
        case SPACE:
        case NEW_LINE:
          // do nothing
          break;
        default:
          // probable not a main function
          main_state = MAIN_START;
          break;
        }
      } while (token.kind != EOF && main_state != MAIN_START && main_state != MAIN_IN);

      if (main_state != MAIN_IN)
      {
        strcat(main_buffer, token.lexeme);
        strcat(buffer, main_buffer);
      }

      token = clex(lexer);
    }

    if (main_state != MAIN_IN)
    {
      strcat(buffer, token.lexeme);
      fflush(stdout);
    }

    if (token.kind == IDENTIFIER)
    {
      switch (vic_state)
      {
      case VIC_START:
        if (strcmp(token.lexeme, "vic_t") == 0)
        {
          vic_state = VIC_CAPTURE_NAME;
        }
        else if (strcmp(token.lexeme, "vic_ef_create") == 0)
        {
          vic_state = VIC_CAPTURE_NAME;
        }
        break;
      case VIC_CAPTURE_NAME:
        current_vic_name = strdup(token.lexeme);
        vic_state = VIC_CAPTURE_EQUAL | VIC_CAPTURE_ROUTINE;
        break;
      case VIC_CAPTURE_CREATION:
        if (strcmp(token.lexeme, "vic_create") == 0)
        {
          vic_state = VIC_CAPTURE_ABSTRACTION;
        }
        break;
      case VIC_CAPTURE_ABSTRACTION:
        if (strcmp(token.lexeme, "EF_THREAD") == 0)
        {
          current_vic_abstraction |= EF_THREAD;
        }
        else if (strcmp(token.lexeme, "EF_PROCESS") == 0)
        {
          current_vic_abstraction |= EF_PROCESS;
        }
        break;
      default:
        if (vic_state & VIC_CAPTURE_ROUTINE)
        {
          vic_lexer_data *data = cc_get_or_insert(&vic_map, current_vic_name, dummy_vic_data);
          data->routine_name = strdup(token.lexeme);
          current_vic_name = NULL;

          vic_state = VIC_FINISH;
        }
        break;
      }
    }
    else if (token.kind == EQUAL)
    {
      if (vic_state & VIC_CAPTURE_EQUAL)
      {
        vic_state = VIC_CAPTURE_CREATION;
      }
    }
    else if (token.kind == CPARAN)
    {
      switch (vic_state)
      {
      case VIC_CAPTURE_ABSTRACTION:
      {
        vic_lexer_data *data = cc_get_or_insert(&vic_map, current_vic_name, dummy_vic_data);
        data->abstraction = current_vic_abstraction;
        current_vic_abstraction = 0;
        current_vic_name = NULL;

        vic_state = VIC_FINISH;
        break;
      }
      case VIC_CAPTURE_ROUTINE:
      {
        vic_state = VIC_FINISH;
        break;
      }
      default:
        free(current_vic_name);
        current_vic_name = NULL;
        current_vic_abstraction = 0;

        vic_state = VIC_START;

        break;
      }
    }
    else if (token.kind == SEMICOL)
    {
      switch (vic_state)
      {
      case VIC_FINISH:
        vic_state = VIC_START;
        break;
      case VIC_START:
        // do nothing
        break;
      default:
        free(current_vic_name);
        current_vic_name = NULL;
        current_vic_abstraction = 0;

        vic_state = VIC_START;

        break;
      }
    }
    else if (token.kind == OCURLYBRACE)
    {
      if (main_state == MAIN_IN)
        main_paran_count++;
    }
    else if (token.kind == CCURLYBRACE)
    {
      if (main_state == MAIN_IN)
      {
        main_paran_count--;

        if (main_paran_count == 0)
          main_state = MAIN_FINISH;
      }
    }

    free(token.lexeme);
    token = clex(lexer);
  }

  cc_for_each(&vic_map, key, val)
  {
    if (strcmp(val->routine_name, "NULL") == 0 || !(val->abstraction & EF_PROCESS))
      continue;

    printf("%s: %d, %s\n", *key, val->abstraction, val->routine_name);

    char name[256] = {};
    strcat(name, output_dir);
    strcat(name, val->routine_name);
    strcat(name, ".c");

    FILE *ofile = fopen(name, "w");
    if (ofile == NULL)
    {
      perror("Error opening file");
      return -1;
    }

    fprintf(ofile, "%s\n", buffer);
    
    char main_buffer[256] = "int main(int argc, char **argv)\n{\n";
    strcat(main_buffer, "  vic_t *main_vic = vic_init();\n");
    strcat(main_buffer, "  ");
    strcat(main_buffer, val->routine_name);
    strcat(main_buffer, "(main_vic);\n");
    strcat(main_buffer, "  vic_destroy(main_vic);\n");
    strcat(main_buffer, "  return 0;\n}\n");

    fprintf(ofile, "%s\n", main_buffer);

    fclose(ofile);
  }

  cc_for_each(&vic_map, key, val)
  {
    free(*key);
    free(val->routine_name);
    val->routine_name = NULL;
  }

  // Clean up
  free((char *)lexer->content);
  free(buffer);

  cc_cleanup(&vic_map);

  clexDeleteKinds(lexer);
}