#include "third_party/clex/clex.h"
#include <assert.h>
#include <string.h>

#include <stdio.h>
#include <ctype.h>

typedef enum TokenKind
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
} TokenKind;

int main1(int argc, char **argv)
{

  clexLexer *lexer = clexInit();

  // clexRegisterKind(lexer, "int", INT);
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

  FILE *ifile, *ofile;
  char *buffer;
  long file_size;

  // Open the file in read mode
  ifile = fopen("/home/artem/projects/ef_lib/main1.c", "r");
  if (ifile == NULL)
  {
    perror("Error opening file");
    return -1;
  }

  ofile = fopen("/home/artem/projects/ef_lib/main1_o.c", "w");
  if (ofile == NULL)
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

  clexReset(lexer, buffer);

  int count = 0;

  clexToken token = clex(lexer);
  while (token.kind != EOF)
  {
    if (iscntrl(*token.lexeme))
    {
      printf("Token: %d , type: %d", (int)*token.lexeme, token.kind);
    }
    else
    {
      printf("Token: \"%s\" , type: %d", token.lexeme, token.kind);
    }

    count++;

    fprintf(ofile, "%s", token.lexeme);

    free(token.lexeme);
    token = clex(lexer);
  }

  // Clean up
  fclose(ifile);
  fclose(ofile);
  free(buffer);

  clexDeleteKinds(lexer);
}