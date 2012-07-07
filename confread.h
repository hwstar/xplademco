
/*
 * confread.h
 * 
 *  Copyright (C) 2012  Stephen Rodgers
 * 
 */

#ifndef CONFSCAN_H
#define CONFSCAN_H

#include "types.h"

/* Enums */

enum {CRE_SYNTAX, CRE_MALLOC, CRE_IO, CRE_FOPEN};

/* Typedefs */

typedef struct keyent KeyEntry_t;
typedef KeyEntry_t * KeyEntryPtr_t;
typedef struct sectionent SectionEntry_t;
typedef SectionEntry_t * SectionEntryPtr_t;
typedef struct configent ConfigEntry_t;
typedef ConfigEntry_t * ConfigEntryPtr_t;

/* Entry for a key table entry */

struct	keyent{
	uint32_t magic;
	uint32_t hash;
	unsigned linenum;
	String key;
	String value;
	KeyEntryPtr_t prev;
	KeyEntryPtr_t next;	
};

/* Entry for a section block table */

struct sectionent{
	uint32_t magic;
	uint32_t hash;
	unsigned linenum;
	String section;
	KeyEntryPtr_t key_head;
	KeyEntryPtr_t key_tail;
	SectionEntryPtr_t prev;
	SectionEntryPtr_t next;	
};


struct configent{
	uint32_t magic;
	String line;
	String work_string;
	SectionEntryPtr_t head;
	SectionEntryPtr_t tail;
};


/* 
*Function prototypes 
*/

/* Config functions */
ConfigEntryPtr_t confreadScan(const String confpath, void (*error_callback)(int type, int linenum, const String info));
void confreadFree(ConfigEntry_t *theConfig);

/* Section functions */
SectionEntryPtr_t confreadFindSection(ConfigEntryPtr_t ce, const String section);
const String confreadGetSection(SectionEntryPtr_t se);
SectionEntryPtr_t confreadGetFirstSection(ConfigEntryPtr_t ce);
SectionEntryPtr_t confreadGetNextSection(SectionEntryPtr_t se);
unsigned confreadSectionLineNum(SectionEntryPtr_t se);

/* Key Functions */
KeyEntryPtr_t confreadFindKey(SectionEntryPtr_t se, const String key);
const String confreadGetKey(KeyEntryPtr_t ke);
KeyEntryPtr_t confreadGetFirstKey(SectionEntryPtr_t se);
KeyEntryPtr_t confreadGetNextKey(KeyEntryPtr_t ke);
unsigned confreadKeyLineNum(KeyEntryPtr_t ke);


/* Value functions */
const String confreadGetValue(KeyEntryPtr_t ke);
const String confreadValueBySectKey(ConfigEntryPtr_t ce, const String section, const String key);
Bool confReadValueBySectKeyAsUnsigned(ConfigEntryPtr_t ce, const String section, const String key, unsigned *res);

/*Default error handler*/
void confReadDefErrorHandler( int etype, int linenum, const String info);

/* Utility Functions */

int confreadSplitArgs(const String string, char sep, String *list, int limit);
String confreadStringCopy(String dest, const String src, int charsToCopy);

/* Debugging functions */
void confreadDebugDump(ConfigEntryPtr_t ce);


#endif	
	
	

