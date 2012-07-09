
/*
 * confread.c Created 7/07/12
 * 
 *  Copyright (C) 2012  Stephen Rodgers
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 * 
 * Stephen "Steve" Rodgers <hwstar@rodgers.sdcoxmail.com>
 *
 */

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "confread.h"
#include "notify.h"

/* Definitions */


#define MAX_CONFIG_LINE	1024
#define MAX_VALUE 768
#define MAX_KEY 128
#define MAX_SECTION 128

#define CE_MAGIC	0x4F8A1C09
#define SE_MAGIC	0x4FCB128D
#define KE_MAGIC	0x4F091E76



/* Scanner tokens */

enum { TOK_ERR = -1, TOK_NL=0, TOK_SECTION, TOK_KEY, TOK_VALUE, TOK_COMMENT};


/* Internal functions */

static int linescan(String *lp, String tokstring);
static String removespctab(String line);
static char copyuntil(String dest, String *srcp, int max_dest_len, const String stopchrs);

/*
* Hash a string
*/

uint32_t confreadHash(const String key)
{
	int len = strlen(key);
	register uint32_t hash, i;

	if(!key)
		return 0;

	for(hash = i = 0; i < len; ++i){
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
 	}
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	return hash;
}


/*
* Copy a string from src pointer to a pointer to dest of a maximum
* specified length looking for one or more stop characters.
* If dest is set to NULL, then throw the characters away, and return on the next stop character.
* Else if dest is non-NULL, do not copy the stop character to the destination
* and always terminate the destination with a NUL character. Return the
* character the copy stopped on. In the case of no stop character
* match, return a nul.
*/

static char copyuntil(String dest, String *srcp, int max_dest_len, const String stopchrs){

	String p = "";
	int i;

	/* Note: max_dest_len check below accounts for NUL at eos. */

	for(i = 0; i < max_dest_len - 1; i++){

		/* If nul at current src string pos, stop copy */
		/* and point p to the NUL character in the stop char string */

		if(**srcp == '\0'){
			p = stopchrs + strlen(stopchrs); /* Point to NUL in string */
			break;
		}

		/* Check for one of the stop characters */

		for(p = stopchrs; *p != '\0'; p++){
			if(**srcp == *p)
				break;
		}

		/* If a stop character was matched, *p will be nz */
		/* if *p is zero, then copy the character to the */
		/* destination string if the destination string is non-NULL */

		if(*p)
			break;	
		else{
			if(dest){
				*dest++ = *(*srcp)++;
			}
			else (*srcp)++;		
		}	
	}

	/* NUL terminate the destination string if dest is non-NULL*/
	
	if(dest)
		*dest = 0;
	return *p; /* Return character which stopped copy */
}

/*
* Remove spaces and tabs from the line in place
*/
			
static String removespctab(String line)
{
	int si = 0, di = 0;

	while(line && line[si]){
		if((line[di] == ' ') || (line[di] == '\t')){
			di++;
		}
		else{
			line[si++] = line[di++];
		}
	}
	return line;
}	


/*
* Scan the line for tokens. Return a token code indicating what was
* found. Load tokstring with the token found unless tokstring is set to NULL, 
* in that case, throw the characters away until the next token is detected.
*/

static int linescan(String *lp, String tokstring){

	int retval = TOK_ERR;
	
	switch(**lp){
		case '\n':
			/* New line */
			debug(DEBUG_INCOMPLETE, "TOK_NL");

			retval = TOK_NL;
			break;

		case ';': 
		case '#':
			debug(DEBUG_INCOMPLETE, "TOK_COMMENT");

			/* Comment */

			retval = TOK_COMMENT;
			break;

		case '=':
			/* Value */
			(*lp)++;
			copyuntil(tokstring, lp, MAX_VALUE, "#;\n");
			debug(DEBUG_INCOMPLETE, "TOK_VALUE");
			retval = TOK_VALUE;
			break;


 		case '[':
			/* Section start */

			(*lp)++;
			if(copyuntil(tokstring, lp, MAX_SECTION, "]\n") == ']'){
				(*lp)++;
				debug(DEBUG_INCOMPLETE, "TOK_SECTION");
				retval = TOK_SECTION;
			}
			else{
				debug(DEBUG_UNEXPECTED, "Section not closed off");
				retval = TOK_ERR; /* Section broken */
			}
			break;

		default:
			/* Look for a key */

			if(isalnum(**lp)){
				debug(DEBUG_INCOMPLETE, "lp: %s", *lp);
				copyuntil(tokstring, lp, MAX_KEY, "=\n");
				debug(DEBUG_INCOMPLETE, "stop char: %s", *lp);

				if(**lp == '='){
					debug(DEBUG_INCOMPLETE, "TOK_KEY");
					retval = TOK_KEY;
				}
				else{
					debug(DEBUG_INCOMPLETE, "TOK_ERR, key broken");
					retval = TOK_ERR; // Key broken
				}
			}
			else{
				debug(DEBUG_INCOMPLETE, "TOK_ERR, invalid char %c", **lp);
				retval = TOK_ERR; // Not something valid
			}
			break;
	}				
	return retval;
}					

/* Global functions */

/*
* Safer string copy
*/

String confreadStringCopy(String dest, const String src, int charsToCopy)
{
	if((!dest) || (!src))
		return NULL;

	strncpy(dest, src, charsToCopy);
	dest[charsToCopy - 1] = 0;
	return dest;
}



/*
* Retrieve a section structure by name. If it doesn't exist, return NULL
*/


SectionEntryPtr_t confreadFindSection(ConfigEntryPtr_t ce, const String section)
{
	uint32_t sh;
	SectionEntryPtr_t se;

	if((!ce) || (ce->magic != CE_MAGIC) || (!ce->head))
		return NULL;

	/* Hash the section string passed in */
	sh = confreadHash(section);
	for(se = ce->head; (se); se = se->next){ /* Traverse section list */
		/* Compare hashes, and if they match, compare strings */
		if((sh == se->hash) && (!strcmp(se->section, section))){
			return se;
		}
	}
	return NULL; /* No match found */
}

/*
* Return the section name, or NULL if it does not exist
*/

const String confreadGetSection(SectionEntryPtr_t se)
{
	if((!se) || (se->magic != KE_MAGIC) || (!se->section))
		return NULL;
	return se->section;
}


/*
* Return a pointer to the first section entry if it exists. If it does not exist, return NULL
*/

SectionEntryPtr_t confreadGetFirstSection(ConfigEntryPtr_t ce)
{
	if((!ce) || (ce->magic != CE_MAGIC) || (!ce->head))
		return NULL;
	return ce->head;
}

/*
* Return a pointer to the next section entry if it exists. If it does not exist, return NULL
*/

SectionEntryPtr_t confreadGetNextSection(SectionEntryPtr_t se)
{
	if((!se) || (se->magic != SE_MAGIC) || (!se->next))
		return NULL;
	return se->next;
}

/*
* Return the line number for the section entry
*/

unsigned confreadSectionLineNum(SectionEntryPtr_t se)
{
	if((!se) || (se->magic != SE_MAGIC))
		return 0;
	return se->linenum;
}



/*
* Return a pointer to the matching key in a section if it exists
*/

KeyEntryPtr_t confreadFindKey(SectionEntryPtr_t se, const String key)
{
	uint32_t kh;
	KeyEntryPtr_t ke;

	if((!se) || (se->magic != SE_MAGIC) || (!se->key_head))
		return NULL;

	/* Hash the section string passed in */
	kh = confreadHash(key);
	for(ke = se->key_head; (ke); ke = ke->next){ /* Traverse key list */
		/* Compare hashes, and if they match, compare strings */
		if((kh == ke->hash) && (!strcmp(ke->key, key)))
			return ke;
	}
	return NULL; /* No match found */
}
/*
* Return a key from a key struct
*/

const String confreadGetKey(KeyEntryPtr_t ke)
{
	if((!ke) || (ke->magic != KE_MAGIC) || (!ke->key))
		return NULL;
	return ke->key;
}


/*
* Return a line number from a key entry
*/

unsigned confreadKeyLineNum(KeyEntryPtr_t ke)
{
	if((!ke) || (ke->magic != KE_MAGIC))
		return 0;
	return ke->linenum;
}


/*
* Return first key structure in a given section if it exists
*/

KeyEntryPtr_t confreadGetFirstKey(SectionEntryPtr_t se)
{

	if((!se) || (se->magic != SE_MAGIC) || (!se->key_head))
		return NULL;

	return se->key_head;
}

/*
* Return the next key structure pointed to by the current key structure if it exists
*/

KeyEntryPtr_t confreadGetNextKey(KeyEntryPtr_t ke)
{
	if((!ke) || (ke->magic != KE_MAGIC) || (!ke->next))
		return NULL;
	return ke->next;
}


/*
* Return a value associated with a key struct
*/

const String confreadGetValue(KeyEntryPtr_t ke)
{
	if((!ke) || (ke->magic != KE_MAGIC) || (!ke->value))
		return NULL;
	return ke->value;

}

/*
* Return key entry by section and key
*/


KeyEntryPtr_t confreadKeyEntryBySectKey(ConfigEntryPtr_t ce, const String section, const String key)
{
	SectionEntryPtr_t se;

	if((!section) || (!key))
		return NULL;

 	se = confreadFindSection(ce, section);
	return confreadFindKey(se, key);

}

/*
* Return first Key in section
*/

KeyEntryPtr_t confreadGetFirstKeyBySection(ConfigEntryPtr_t ce, const String section)
{
	SectionEntryPtr_t se = confreadFindSection(ce, section);
	return confreadGetFirstKey(se);
}

/*
* Return a count of the number of entries in a section
*/

unsigned confreadGetNumEntriesInSect(ConfigEntryPtr_t ce, const String section)
{
	SectionEntryPtr_t se = confreadFindSection(ce, section);
	if(se)
		return se->entry_count;
	else
		return 0;
}


/*
* Find a value by section and key
*/

const String confreadValueBySectKey(ConfigEntryPtr_t ce, const String section, const String key)
{
	KeyEntryPtr_t ke = confreadKeyEntryBySectKey(ce, section, key);
	return confreadGetValue(ke);

}

/*
* Find value by section and key, convert to unsigned int, return in res. 
*/

Bool confReadValueBySectKeyAsUnsigned(ConfigEntryPtr_t ce, const String section, const String key, unsigned *res)
{
	String num = confreadValueBySectKey(ce, section, key);
	if(num && res){
		long val = strtol(num, NULL, 0);
		if((errno != ERANGE) && (val > 0) && (val <= UINT_MAX)){
			*res = (unsigned) val;
			return TRUE;
		}
	}

	return FALSE;
}


/*
* Default error handler for confreadScan()
*/

void confReadDefErrorHandler( int etype, int linenum, String info)
{
	switch(etype){

		case CRE_MALLOC:
			error("Memory allocation error in confread.c, line %d", linenum);
			break;

		case CRE_SYNTAX:
			error("Syntax error in config file on line: %d", linenum);
			break;

		case CRE_IO:
			error("I/O error in confead.c: %s", info);
			break;

		case CRE_FOPEN:
			error("Could not open config file at: %s", info);
			break;

		default:
			error("Unknown error code: %d", etype);
			break;

	}


}



/*
* Free all data structures associated with our config files
*/

void confreadFree(ConfigEntryPtr_t ce)
{
	SectionEntryPtr_t se, sef;
	KeyEntryPtr_t kv, kvf;

	if((!ce) || (ce->magic != CE_MAGIC))
		return;

	se = ce->tail; /* Start at end of section list and work back */
	
	while((se) && (se->magic == SE_MAGIC)){
		se->magic = 0; /* Clear the magic # */
		kv = se->key_tail; /* Start at end of kv list and work back */
		while((kv) && (kv->magic == KE_MAGIC)){
			kv->magic = 0; /* Clear the magic # */
			if(kv->value)
				free(kv->value);
			if(kv->key)
				free(kv->key);
			kvf = kv; /* note this structure for freeing */
			kv = kv->prev;
			free(kvf);
		}
		if(se->section)
			free(se->section);
		sef = se; /* note this structure for freeing */
		se = se->prev;
		free(sef);
	}
	if(ce->line)
		free(ce->line);
	if(ce->work_string)
		free(ce->work_string);
	ce->magic = 0; /* Clear the magic # */
	free(ce);
}


/*
* Dump all printable fields in all data structures associated with the config file
*/

void confreadDebugDump(ConfigEntryPtr_t ce)
{
	SectionEntryPtr_t se;
	KeyEntryPtr_t kv;

	if((!ce) || (ce->magic != CE_MAGIC))
		return;

	se = ce->head; /* Start at beginning of section list and work forward */
	
	while((se) && (se->magic == SE_MAGIC)){
		if(se->section)
			printf("**** Section: %s Hash: %08X Line Number: %d ****\n", se->section, se->hash, se->linenum);
		else
			printf("!!!! NULL Section string on line number %d !!!!\n", se->linenum);

		kv = se->key_head; /* Start at beginning of kv list and work forward */
		while((kv) && (kv->magic == KE_MAGIC)){
			if(kv->key)
				printf("Key: %s Hash: %08X Line Number %d ", kv->key, kv->hash, kv->linenum);
			else
				printf("!! NULL Key on line number %d", kv->linenum);
			if(kv->value)
				printf(" Value: %s\n",kv->value);
			else
				printf(" !! NULL Value\n");
			kv = kv->next;		
		}
		se = se->next;
	}
}


/*
* Scan a config file and load it into our data structures
* Pass in the path to the config file, and optionally an error handling function.
* If the default error handling function is going to be used, then pass in a NULL for the
* second argument.
*/


ConfigEntryPtr_t confreadScan(String thePath, void (*error_callback)(int type, int linenum, String info )){
	FILE *conf_file;
	String p;
	ConfigEntryPtr_t ce = NULL;
	SectionEntryPtr_t se = NULL;
	KeyEntryPtr_t kv = NULL;
	int linenum;

	/* User our built in handler if no error handler is specified */

	if(!error_callback)
		error_callback = confReadDefErrorHandler;

	/* Allocate a config entry */

	if(!(ce = malloc(sizeof(ConfigEntry_t)))){
		debug(DEBUG_UNEXPECTED, "Can't malloc config entry in confReadScan()");
		(*error_callback)(CRE_MALLOC, __LINE__, NULL);
		return NULL;
	}
	
	/* Initialize config entry */
	memset(ce, 0, sizeof(ConfigEntry_t));
	ce->magic = CE_MAGIC;

	
	/* Allocate a line buffer */

	if(!(ce->line = malloc(MAX_CONFIG_LINE))){
		debug(DEBUG_UNEXPECTED, "Can't malloc line buffer in confReadScan()");
		confreadFree(ce);
		(*error_callback)(CRE_MALLOC, __LINE__, NULL);
		return NULL;
	}

	/* Allocate a line buffer */

	if(!(ce->work_string = malloc(MAX_CONFIG_LINE))){
		debug(DEBUG_UNEXPECTED, "Can't malloc work string in confReadScan()");
		confreadFree(ce);
		(*error_callback)(CRE_MALLOC, __LINE__, NULL);
		return NULL;
	}


	/* Open the config file */

	if((conf_file = fopen(thePath, "r")) == NULL){
		(*error_callback)(CRE_FOPEN, __LINE__, thePath);
		return NULL;
	}
	
	for(linenum = 1; ; linenum++){
		/* Get a line */
		if(fgets(ce->line, MAX_CONFIG_LINE, conf_file) == NULL)
			break;

		/* Remove spaces and tabs */
		removespctab(ce->line);

		
		p = ce->line;
		
		/* Parse tree root */

		switch(linescan(&p, ce->work_string)){

			/* It was a newline or a comment, get another line */

			case TOK_NL:
			case TOK_COMMENT:
				break;

			/* It was a section ID. Get it */
			
			case TOK_SECTION:

				if(!(se = malloc(sizeof(SectionEntry_t)))){
					confreadFree(ce);
					(*error_callback)(CRE_MALLOC, __LINE__, NULL);
					return NULL;
				}
				/* Initialize section entry */
				memset(se, 0, sizeof(SectionEntry_t));
				se->magic = SE_MAGIC;

				/* Copy the section name into the new entry */
				if(!(se->section = strdup(ce->work_string))){
					confreadFree(ce);
					(*error_callback)(CRE_MALLOC, __LINE__, NULL);
					return NULL;
				}
				/* Hash the section */
				se->hash = confreadHash(se->section);

				/* Record the line number */
				se->linenum = linenum;
	
				/* Scan rest of line looking for a comment or a new line */

				switch(linescan(&p, NULL)){
					case TOK_NL:
					case TOK_COMMENT:
						break;

					default:
						debug(DEBUG_UNEXPECTED,"only newline or comment token is valid after a section token");
						(*error_callback)(CRE_SYNTAX, linenum, NULL);
						return NULL;
				}

				/* Insert into section list */
				if(!ce->head){
					ce->head = se; /* First entry */
				}
				else{
					ce->tail->next = se; /* Subsequent entry */
					se->prev = ce->tail;
				}
				ce->tail = se;
				break; 


			case TOK_KEY:

				kv = NULL;
				if(se){	/* There has to be a section defined */
					if(!(kv = malloc(sizeof(KeyEntry_t)))){
						confreadFree(ce);
						(*error_callback)(CRE_MALLOC, __LINE__, NULL);
						return NULL;
					}
		
					/* Initialize section entry */
					memset(kv, 0, sizeof(KeyEntry_t));
					kv->magic = KE_MAGIC;

					/* Save the key */
					if(!(kv->key = strdup(ce->work_string))){
						confreadFree(ce);
						(*error_callback)(CRE_MALLOC, __LINE__, NULL);
						return NULL;
					}

					/* Hash the key */
					kv->hash = confreadHash(kv->key);

					/* Record the line number */
					kv->linenum = linenum;
				}

				/* Next token had better be a value */

				switch(linescan(&p, ce->work_string)){
					case TOK_VALUE:
						if(kv && se){
							/* Save value */
							if(!(kv->value = strdup(ce->work_string))){
								confreadFree(ce);
								(*error_callback)(CRE_MALLOC, __LINE__, NULL);
								return NULL;
							}
							/* Count the new entry */
							se->entry_count++;
							/* Insert new key/value into list in current section */
							if(!se->key_head){
								se->key_head = kv; /* First entry */
							}
							else{
								se->key_tail->next = kv; /* Subsequent entry */
								kv->prev = se->key_tail;
							}
							se->key_tail = kv;
								
						}
						break;
					default:
						debug(DEBUG_UNEXPECTED, "should have received a value token");
						(*error_callback)(CRE_SYNTAX, linenum, NULL);
						return NULL;
				}

				/* Next token had better be a */
				/* newline or comment */

				switch(linescan(&p, NULL)){
					case TOK_NL:
					case TOK_COMMENT:
						break;
					default:
						debug(DEBUG_UNEXPECTED, "invalid token found while parsing a key/value");
						(*error_callback)(CRE_SYNTAX, linenum, NULL);
						return NULL;

				}
				break;
			case TOK_ERR:
				debug(DEBUG_UNEXPECTED, "TOK_ERR returned from linescan()");
				(*error_callback)(CRE_SYNTAX, linenum, NULL);
				return NULL;


			default:
				debug(DEBUG_UNEXPECTED,"unexpected token returned");
				(*error_callback)(CRE_SYNTAX, linenum, NULL);
				return NULL;

		}
	
	}			 

	if(ferror(conf_file)){
		(*error_callback)(CRE_IO, __LINE__, strerror(errno));
		return NULL;		
	}

	else
		fclose(conf_file);
	return ce;
}


