// vi: set ts=4 sw=4 :
// vim: set tw=75 :

// singlep.cpp - singleplayer support

/*
 * Copyright (c) 2002-2003 Pierre-Marie Baty <pm@racc-ai.com>
 * Copyright (c) 2003 Will Day <willday@hpgx.net>
 *
 *    This file is part of Metamod.
 *
 *    Metamod is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published by the
 *    Free Software Foundation; either version 2 of the License, or (at
 *    your option) any later version.
 *
 *    Metamod is distributed in the hope that it will be useful, but
 *    WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with Metamod; if not, write to the Free Software Foundation,
 *    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    In addition, as a special exception, the author gives permission to
 *    link the code of this program with the Half-Life Game Engine ("HL
 *    Engine") and Modified Game Libraries ("MODs") developed by Valve,
 *    L.L.C ("Valve").  You must obey the GNU General Public License in all
 *    respects for all of the code used other than the HL Engine and MODs
 *    from Valve.  If you modify this file, you may extend this exception
 *    to your version of the file, but you are not obligated to do so.  If
 *    you do not wish to do so, delete this exception statement from your
 *    version.
 *
 */

#include <stdio.h>			// fopen, etc
#include <string.h>			// strstr

#include <extdll.h>			// always

#include <meta_api.h>		// gpMetaUtilFuncs
#include <mutil.h>			// GET_GAME_INFO
#include <support_meta.h>	// STRNCPY
#include <osdep.h>			// DLOPEN, DLSYM

#include "singlep.h"		// me

//! Holds engine functionality callbacks
enginefuncs_t g_engfuncs;
globalvars_t  *gpGlobals;

// Description of plugin.
// (V* info from info_name.h)
plugin_info_t Plugin_info = {
	META_INTERFACE_VERSION, // ifvers
	"SinglePlayer",		// name
	"2.0",				// version
	__DATE__,			// date
	"Pierre-Marie Baty <pm@racc-ai.com>, Will Day <willday@metamod.org>",		// author
	"http://github.com/CecilHarvey/singlep-mm/",			// url
	"SINGLEP",		// logtag
	PT_ANYTIME,		// loadable
	PT_ANYPAUSE,	// unloadable
};

cvar_t init_plugin_debug = {"sp_debug", "0", FCVAR_EXTDLL, 0, NULL};

// Global variables from metamod.  These variable names are referenced by
// various macros.
meta_globals_t *gpMetaGlobals;		// metamod globals
gamedll_funcs_t *gpGamedllFuncs;	// gameDLL function tables
mutil_funcs_t *gpMetaUtilFuncs;		// metamod utility functions

// Metamod requesting info about this plugin
//  ifvers			(given) interface_version metamod is using
//  pPlugInfo		(requested) struct with info about plugin
//  pMetaUtilFuncs	(given) table of utility functions provided by metamod
C_DLLEXPORT int Meta_Query(const char *ifvers, plugin_info_t **pPlugInfo,
		mutil_funcs_t *pMetaUtilFuncs) 
{
	// Check for valid pMetaUtilFuncs before we continue.
	if (!pMetaUtilFuncs) {
		LOG_ERROR(PLID, "[%s] ERROR: Meta_Query called with null pMetaUtilFuncs\n", Plugin_info.logtag);
		return FALSE;
	}
	gpMetaUtilFuncs = pMetaUtilFuncs;

	// Give metamod our plugin_info struct.
	*pPlugInfo = &Plugin_info;

	// Check for interface version compatibility.
	if (!FStrEq(ifvers, Plugin_info.ifvers)) {
		int mmajor = 0, mminor = 0, pmajor = 0, pminor = 0;
		LOG_MESSAGE(PLID, "WARNING: meta-interface version mismatch; requested=%s ours=%s",
				Plugin_info.logtag, ifvers);
		// If plugin has later interface version, it's incompatible (update
		// metamod).
		sscanf(ifvers, "%d:%d", &mmajor, &mminor);
		sscanf(META_INTERFACE_VERSION, "%d:%d", &pmajor, &pminor);
		if (pmajor > mmajor || (pmajor == mmajor && pminor > mminor)) {
			LOG_ERROR(PLID, "metamod version is too old for this plugin; update metamod");
			return FALSE;
		}
		// If plugin has older major interface version, it's incompatible
		// (update plugin).
		else if (pmajor < mmajor) {
			LOG_ERROR(PLID, "metamod version is incompatible with this plugin; please find a newer version of this plugin");
			return FALSE;
		}
		// Minor interface is older, but this is guaranteed to be backwards
		// compatible, so we warn, but we still accept it.
		else if (pmajor == mmajor && pminor < mminor)
			LOG_MESSAGE(PLID, "WARNING: metamod version is newer than expected; consider finding a newer version of this plugin");
		else
			LOG_ERROR(PLID, "unexpected version comparison; metavers=%s, mmajor=%d, mminor=%d; plugvers=%s, pmajor=%d, pminor=%d", ifvers, mmajor, mminor, META_INTERFACE_VERSION, pmajor, pminor);
	}

	return TRUE;
}

// Metamod attaching plugin to the server.
//  now				(given) current phase, ie during map, during changelevel, or at startup
//  pFunctionTable	(requested) table of function tables this plugin catches
//  pMGlobals		(given) global vars from metamod
//  pGamedllFuncs	(given) copy of function tables from game dll
C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS *pFunctionTable, 
		meta_globals_t *pMGlobals, gamedll_funcs_t *pGamedllFuncs)
{
	if (now > Plugin_info.loadable) {
		LOG_ERROR(PLID, "Can't load plugin right now");
		return FALSE;
	}
	if (!pMGlobals) {
		LOG_ERROR(PLID, "Meta_Attach called with null pMGlobals");
		return FALSE;
	}
	gpMetaGlobals = pMGlobals;
	if (!pFunctionTable) {
		LOG_ERROR(PLID, "Meta_Attach called with null pFunctionTable");
		return FALSE;
	}

	pFunctionTable->pfnGetEngineFunctions = GetEngineFunctions;
	gpGamedllFuncs = pGamedllFuncs;

	sp_load_gamedll_symbols();
	return TRUE;
}

// Metamod detaching plugin from the server.
// now		(given) current phase, ie during map, etc
// reason	(given) why detaching (refresh, console unload, forced unload, etc)
C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME now, PL_UNLOAD_REASON reason) {
	if (now > Plugin_info.unloadable && reason != PNL_CMD_FORCED) {
		LOG_ERROR(PLID, "Can't unload plugin right now");
		return(FALSE);
	}
	sp_unload_gamedll_symbols();
	return TRUE;
}

#ifndef _WIN32

void *gamedll_handle;

void sp_load_gamedll_symbols(void) {
	char gamedll_pathname[1024];
	STRNCPY(gamedll_pathname, GET_GAME_INFO(PLID, GINFO_DLL_FULLPATH), 
			sizeof(gamedll_pathname));
	gamedll_handle = dlopen(gamedll_pathname, RTLD_LAZY);
}

uint32 sp_FunctionFromName(const char *pName) {
	return (uint32)DLSYM(gamedll_handle, pName);
}

const char *sp_NameForFunction(uint32 function) {
	Dl_info info = {0};
	dladdr((void *)function, &info);
	if (info.dli_sname) {
		return info.dli_sname;
	}
	return NULL;
}

void sp_unload_gamedll_symbols(void) {
	dlclose(gamedll_handle);
	gamedll_handle = NULL;
}

#else

// single player support by Pierre-Marie Baty <pm@racc-ai.com>
WORD *p_Ordinals = NULL;
DWORD *p_Functions = NULL;
DWORD *p_Names = NULL;
char *p_FunctionNames[4096]; // that should be enough, goddamnit !
int num_ordinals = 0;
unsigned long base_offset = 0;

mBOOL dlclose_handle_invalid;

void sp_load_gamedll_symbols(void) {
	// the purpose of this function is to perfect the hooking DLL
	// interfacing. Having all the MOD entities listed and linked to their
	// proper function with LINK_ENTITY_TO_FUNC is not enough, procs are
	// missing, and that's the reason why most hooking DLLs don't allow to
	// run single player games. This function loads the symbols in the game
	// DLL by hand, strips their MSVC-style case mangling, and builds an
	// exports array which supercedes the one the engine would get
	// afterwards from the MOD DLL, which can't pass through the bot DLL.
	// This way we are sure that *nothing is missing* in the interfacing.

	FILE *fp;
	DOS_HEADER dos_header;
	LONG nt_signature;
	PE_HEADER pe_header;
	SECTION_HEADER section_header;
	OPTIONAL_HEADER optional_header;
	LONG edata_offset;
	LONG edata_delta;
	EXPORT_DIRECTORY export_directory;
	LONG name_offset;
	LONG ordinal_offset;
	LONG function_offset;
	char function_name[256];
	int i;
	void *game_GiveFnptrsToDll;
	char gamedll_pathname[1024];
	DLHANDLE gamedll_handle;

	STRNCPY(gamedll_pathname, GET_GAME_INFO(PLID, GINFO_DLL_FULLPATH), 
			sizeof(gamedll_pathname));

	for (i = 0; i < num_ordinals; i++)
		// reset function names array
		p_FunctionNames[i] = NULL;

	// open MOD DLL file in binary read mode
	fp = fopen (gamedll_pathname, "rb");
	// get the DOS header
	fread (&dos_header, sizeof (dos_header), 1, fp);

	fseek (fp, dos_header.e_lfanew, SEEK_SET);
	// get the NT signature
	fread (&nt_signature, sizeof (nt_signature), 1, fp);
	// get the PE header
	fread (&pe_header, sizeof (pe_header), 1, fp);
	// get the optional header
	fread (&optional_header, sizeof (optional_header), 1, fp);

	// no edata by default
	edata_offset = optional_header.DataDirectory[0].VirtualAddress;
	edata_delta = 0; 

	// cycle through all sections of the PE header to look for edata
	for (i = 0; i < pe_header.NumberOfSections; i++)
		if (strcmp ((char *) section_header.Name, ".edata") == 0)
		{
			// if found, save its offset
			edata_offset = section_header.PointerToRawData;
			edata_delta = section_header.VirtualAddress - section_header.PointerToRawData;
		}

	fseek (fp, edata_offset, SEEK_SET);
	// get the export directory
	fread (&export_directory, sizeof (export_directory), 1, fp);

	// save number of ordinals
	num_ordinals = export_directory.NumberOfNames;

	// save ordinals offset
	ordinal_offset = export_directory.AddressOfNameOrdinals - edata_delta;
	fseek (fp, ordinal_offset, SEEK_SET);
	// allocate space for ordinals
	p_Ordinals = (WORD *) malloc (num_ordinals * sizeof (WORD));
	// get the list of ordinals
	fread (p_Ordinals, num_ordinals * sizeof (WORD), 1, fp);

	// save functions offset
	function_offset = export_directory.AddressOfFunctions - edata_delta;
	fseek (fp, function_offset, SEEK_SET);
	// allocate space for functions
	p_Functions = (DWORD *) malloc (num_ordinals * sizeof (DWORD));
	// get the list of functions
	fread (p_Functions, num_ordinals * sizeof (DWORD), 1, fp);

	// save names offset
	name_offset = export_directory.AddressOfNames - edata_delta;
	fseek (fp, name_offset, SEEK_SET);
	// allocate space for names
	p_Names = (DWORD *) malloc (num_ordinals * sizeof (DWORD));
	// get the list of names
	fread (p_Names, num_ordinals * sizeof (DWORD), 1, fp);

	// cycle through all function names and fill in the exports array
	for (i = 0; i < num_ordinals; i++)
	{
		if (fseek (fp, p_Names[i] - edata_delta, SEEK_SET) != -1)
		{
			char *cp, *fname;
			int len;
			len=fread(function_name, sizeof(char), sizeof(function_name)-1, 
					fp);
			function_name[len-1]='\0';
			LOG_DEVELOPER(PLID, "Found '%s'", function_name);

			fname=function_name;
			// is this a MSVC C++ mangled name ?
			// skip leading '?'
			if (fname[0]=='?') fname++;
			// strip off after "@@"
			if ((cp=strstr(fname, "@@")))
				*cp='\0';
			p_FunctionNames[i]=strdup(fname);
			LOG_DEVELOPER(PLID, "Stored '%s'", p_FunctionNames[i]);
		}
	}

	fclose (fp); // close MOD DLL file

	// cycle through all function names to find the GiveFnptrsToDll function
	for (i = 0; i < num_ordinals; i++)
	{
		if (strcmp ("GiveFnptrsToDll", p_FunctionNames[i]) == 0)
		{
			gamedll_handle = DLOPEN(gamedll_pathname);
			game_GiveFnptrsToDll = (void *) DLSYM (gamedll_handle, "GiveFnptrsToDll");
			DLCLOSE(gamedll_handle);
			base_offset = (unsigned long) (game_GiveFnptrsToDll) - p_Functions[p_Ordinals[i]];
			break; // base offset has been saved
		}
	}
	for (i = 0; i < num_ordinals; i++) {
		LOG_DEVELOPER(PLID, "%s %ld", p_FunctionNames[i],
				p_Functions[p_Ordinals[i]] + base_offset);
	}
}

uint32 sp_FunctionFromName(const char *pName) {
	// this function returns the address of a certain function in the exports 
	// array.
	LOG_DEVELOPER(PLID, "FunctionFromName: find '%s'", pName);
	for (int i = 0; i < num_ordinals; i++) {
#if 0
		if(strmatch(pName, "RampThink@CAmbientGeneric")) {
			LOG_DEVELOPER(PLID, "ramp =? '%s'", p_FunctionNames[i]);
		}
#endif
		if (strcmp (pName, p_FunctionNames[i]) == 0) {
			LOG_DEVELOPER(PLID, "Function '%s' at location %ld", pName,
					p_Functions[p_Ordinals[i]] + base_offset);
			// return the address of that function
			RETURN_META_VALUE(MRES_SUPERCEDE, 
					p_Functions[p_Ordinals[i]] + base_offset);
		}
		else
			LOG_DEVELOPER(PLID, "FunctionFromName: '%s' not found", pName);
	}
	// couldn't find the function name to return address
	RETURN_META_VALUE(MRES_SUPERCEDE, 0);
}

const char *sp_NameForFunction(uint32 function) {
	// this function returns the name of the function at a certain address in 
	// the exports array.

	for (int i = 0; i < num_ordinals; i++) {
		if ((function - base_offset) == p_Functions[p_Ordinals[i]]) {
			LOG_DEVELOPER(PLID, "Function at location %d is '%s'",
					function, p_FunctionNames[i]);
			// return the name of that function
			RETURN_META_VALUE(MRES_SUPERCEDE, p_FunctionNames[i]); 
		}
		else
			LOG_DEVELOPER(PLID, "NameForFunction: location %ld not found", 
					function);
	}
	// couldn't find the function address to return name
	RETURN_META_VALUE(MRES_SUPERCEDE, 0);
}
 
void sp_unload_gamedll_symbols(void) {
	if (p_Ordinals) {
		free (p_Ordinals);
		p_Ordinals=NULL;
	}
	if (p_Functions) {
		free (p_Functions);
		p_Functions=NULL;
	}
	if (p_Names) {
		free (p_Names);
		p_Names=NULL;
	}
	for (int i = 0; i < num_ordinals; i++) {
		if (p_FunctionNames[i]) {
			// free the table of exported symbols
			free (p_FunctionNames[i]);
			p_FunctionNames[i]=NULL;
		}
	}
	num_ordinals = 0;
}

#endif

C_DLLEXPORT int GetEngineFunctions(enginefuncs_t *pengfuncsFromEngine, 
		int *interfaceVersion) 
{
	pengfuncsFromEngine->pfnFunctionFromName = sp_FunctionFromName;
	pengfuncsFromEngine->pfnNameForFunction = sp_NameForFunction;

	return TRUE;
}

// Receive engine function table from engine.
// This appears to be the _first_ DLL routine called by the engine, so we
// do some setup operations here.
void WINAPI GiveFnptrsToDll(enginefuncs_t* pengfuncsFromEngine, globalvars_t *pGlobals) {
	memcpy(&g_engfuncs, pengfuncsFromEngine, sizeof(enginefuncs_t));
	gpGlobals = pGlobals;
}
