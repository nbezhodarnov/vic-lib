#define _CRT_SECURE_NO_WARNINGS
#include "clex.h"
#include "fa.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

clexLexer *clexInit(void) {
  clexLexer *lexer = malloc(sizeof(clexLexer));
  lexer->rules = NULL;
  lexer->content = NULL;
  lexer->position = 0;

  clexRegisterKind(lexer, "\n", NEW_LINE);
  clexRegisterKind(lexer, " ", SPACE);

  return lexer;
}

void clexReset(clexLexer *lexer, const char *content) {
  lexer->content = content;
  lexer->position = 0;
  lexer->max_position = strlen(content);
}

bool clexRegisterKind(clexLexer *lexer, const char *re, int kind) {
  if (!lexer->rules)
    lexer->rules = calloc(CLEX_MAX_RULES, sizeof(clexRule *));
  for (int i = 0; i < CLEX_MAX_RULES; i++) {
    if (!lexer->rules[i]) {
      clexRule *rule = malloc(sizeof(clexRule));
      rule->re = re;
      rule->nfa = clexNfaFromRe(re, NULL);
      if (!rule->nfa)
        return false;
      rule->kind = kind;
      lexer->rules[i] = rule;
      break;
    }
  }
  return true;
}

void clexDeleteKinds(clexLexer *lexer) {
  if (lexer->rules)
    memset(lexer->rules, 0, CLEX_MAX_RULES);
}

clexToken clex(clexLexer *lexer) {
  if (lexer->position >= lexer->max_position)
    return (clexToken){.lexeme = NULL, .kind = EOF};

  // while (isspace(lexer->content[lexer->position]) && !iscntrl(lexer->content[lexer->position]))
  //   lexer->position++;
  size_t start = lexer->position;
  while (lexer->content[lexer->position] != '\0' &&
         !isspace(lexer->content[++lexer->position]))
    ;
  
  size_t position = lexer->position;
  char *part = calloc(position - start + 1, sizeof(char));
  strncpy(part, lexer->content + start, position - start);

  while (strlen(part)) {
    for (int i = 0; i < CLEX_MAX_RULES; i++)
      if (lexer->rules[i])
        if (clexNfaTest(lexer->rules[i]->nfa, part)) {
          lexer->position = position;
          return (clexToken){.lexeme = part, .kind = lexer->rules[i]->kind};
        }
    part[strlen(part) - 1] = '\0';
    position--;
    if (isspace(lexer->content[position]))
      position--;
  }

  strncpy(part, lexer->content + start, lexer->position - start);
  return (clexToken){.lexeme = part, .kind = UNDEFINED};
}

clexToken clexNoSpace(clexLexer *lexer) {
  clexToken token = clex(lexer);
  
  while (token.kind == SPACE)
    token = clex(lexer);
  
  return token;
}