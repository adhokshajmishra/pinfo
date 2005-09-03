/***************************************************************************
 *  Pinfo is a ncurses based lynx style info documentation browser
 *
 *  Copyright (C) 1999  Przemek Borys <pborys@dione.ids.pl>
 *  Copyright (C) 2005  Bas Zoetekouw <bas@debian.org>
 *  Copyright 2005  Nathanael Nerode <neroden@gcc.gnu.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 ***************************************************************************/

#include "common_includes.h"
#include "utils.h"
#include "tmpfiles.h"
#include <string>
using std::string;
#include <vector>
using std::vector;

void
basename_and_dirname(const string filename, string& basename, string& dirname)
{
	/* Dirname should end with a slash, or be empty. */
	string::size_type index = filename.rfind('/');
	if (index == string::npos) {
		basename = filename;
		dirname = "";
	} else {
		basename = filename.substr(index + 1);
		dirname = filename.substr(0, index + 1);
	}
}

void
basename(const string filename, string& basename_str)
{
	string::size_type index = filename.rfind('/');
	if (index == string::npos) {
		basename_str = filename;
	} else {
		basename_str = filename.substr(index + 1);
	}
}

/* In this one, dirname *doesn't* have a trailing slash. */
void
dirname(const string filename, string& dirname_str)
{
	string::size_type index = filename.rfind('/');
	if (index == string::npos) {
		dirname_str = "";
	} else {
		dirname_str = filename.substr(0, index);
	}
}


/******************************************************************************
 * This piece of declarations says what to do with info files stored with      *
 * different formats/compression methods, before putting them into a temporary *
 * file. I.e. you don't do anything to plain `.info' suffix; for a `.info.gz'  *
 * you dump the file through `gunzip -d -c', etc.                              *
 ******************************************************************************/

typedef struct Suffixes
{
	const char * const suffix;
	const char * const command;
}
Suffixes;

#define SuffixesNumber 4

static const Suffixes suffixes[SuffixesNumber] =
{
	{"", 		"cat"},
	{".gz",		"gzip -d -q -c"},
	{".Z",		"gzip -d -q -c"},
	{".bz2",	"bzip2 -d -c"}
};

/*****************************************************************************/

vector<string> infopaths;

void
sort_tag_table(void) {
	if (!tag_table.empty())
		std::sort(tag_table.begin(), tag_table.end(), compare_tags);
}

/*
 * Looks for name_string -- appended to buf!
 * Returns 0 if it finds a match, 1 if not.
 * Leaves the matching name in buf.
 */
int
matchfile(string& buf, const string name_string)
{
	string basename_string;
	string dirname_string;
	basename_and_dirname(name_string, basename_string, dirname_string);

	if (buf[buf.length()-1]!='/')
		buf += "/";
	buf += dirname_string;

	DIR *dir;
	dir = opendir(buf.c_str());	/* here we always have '/' at end */
	if (dir == NULL)
		return 1;

	struct dirent *dp;
	while ((dp = readdir(dir))) { /* Ends loop when NULL is returned */
		string test_filename = dp->d_name;
		strip_compression_suffix(test_filename); /* Strip in place */
		string basename_info = basename_string;
		basename_info += ".info";
		if (test_filename  == basename_info) {
			/* Matched.  Clean up and return from function. */
			buf += "/";
			buf += test_filename;
			closedir(dir);
			return 0;
		}
	}
	closedir(dir);
	return 1;
}

FILE *
dirpage_lookup(char **type, char ***message, long *lines,
		const string filename, string& first_node)
{
#define Type	(*type)
#define Message	(*message)
#define Lines	(*lines)
	FILE *id = 0;
	bool goodHit = false;
	id = opendirfile(0);
	if (!id)
		return 0;
	read_item(id, type, message, lines);
	for (int i = 1; i < Lines; i++)	{ /* search through all lines */
		/* we want: `* name:(file)node.' */
		string this_line = Message[i];
		if (    (this_line.length() < 2)
		     || (this_line[0] != '*')
		     || (this_line[1] != ' ')
		   ) {
			continue;
		}
		if (this_line.compare(2, filename.length(), filename) != 0) {
			/* Wrong file */
			continue;
		}
		string::size_type nameend = this_line.find(':');
		if (    (nameend == string::npos)
			   || (this_line.length() == nameend + 1)
			   || (this_line[nameend + 1] == ':')
			   ) {
			continue;
		}
		string::size_type filestart = this_line.find('(', nameend + 1);
		if (filestart == string::npos) {
			continue;
		}
		string::size_type fileend = this_line.find(')', filestart);
		if (fileend == string::npos) {
			continue;
		}
		string::size_type dot = this_line.find('.', fileend);
		if (dot == string::npos) {
			continue;
		}
		/* It looks like a match. */
		string file(this_line, filestart + 1, fileend - filestart - 2);
		string name(this_line, fileend + 1, dot - fileend - 2);
		if (name != "") {
			string::size_type idx = 0;
			while (isspace(name[idx]))
				idx++;
			first_node = name.substr(idx);
		}
		if (id)
			fclose(id);	/* we don't need dirfile/badly matched infofile open anymore */
		id = 0;
		if (file.find(".info") == string::npos) {
			file += ".info";
		}
		id = openinfo(file, 0);
		goodHit = true;
		if ((nameend - 2) == filename.length()) {
				/* the name matches perfectly to the query */
				/* stop searching for another match, and use this one */
				break;	
			}
	}
	if (!goodHit)
	{
		fclose(id);
		id = 0;
	}
	return id;
#undef Lines
#undef Message
#undef Type
}

void
freeitem(char **type, char ***buf, long *lines)
{
#define Type	(*type)
#define Buf		(*buf)
#define Lines	(*lines)
	long i;

	if (Type != 0)
	{
		xfree(Type);
		Type = 0;
	}
	if (Buf != 0)
	{
		for (i = 1; i <= Lines; i++)
			if (Buf[i] != 0)
			{
				xfree(Buf[i]);
				Buf[i] = 0;
			}
		xfree(Buf);
		Buf = 0;
	}
#undef Type
#undef Buf
#undef Lines
}

void
read_item(FILE * id, char **type, char ***buf, long *lines)
{

#define Type	(*type)
#define Buf		(*buf)
#define Lines	(*lines)
	int i;

	freeitem(type, buf, lines);	/* free previously allocated memory */

	/* seek precisely on the INFO_TAG (the seeknode function may be imprecise
	 * in combination with some weird tag_tables).  */
	while (fgetc(id) != INFO_TAG);
	/* then skip the trailing `\n' */
	while (fgetc(id) != '\n');

	/* allocate and read the header line */
	Type = (char*)xmalloc(1024);
	fgets(Type, 1024, id);
	Type = (char*)xrealloc(Type, strlen(Type) + 1);

	/* set number of lines to 0 */
	Lines = 0;

	/* initial buffer allocation */
	Buf = (char**)xmalloc(sizeof(char **));

	/* now iterate over the lines */
	do
	{
		/* don't read after eof in info file */
		if (feof(id))
			break;

		/* realloc the previous line for it to fit exactly */
		if (Lines)
		{
			Buf[Lines] = (char*)xrealloc(Buf[Lines], strlen(Buf[Lines]) + 1);
		}

		/* increase the read lines number */
		Lines++;

		/* allocate space for the new line */
		Buf = (char**)xrealloc(Buf, sizeof(char **) *(Lines + 1));
		Buf[Lines] = (char*)xmalloc(1024);
		Buf[Lines][0] = 0;

		/* if the line was not found in input file, fill the allocated space
		 * with empty line.  */
		if (fgets(Buf[Lines], 1024, id) == NULL)
			strcpy(Buf[Lines], "\n");
		else /* we can be sure that at least 1 char was read! */
		{
			/* *sigh*  indices contains \0's
			 * which totally fucks up all strlen()s.
			 * so replace it by a space */
			i = 1023;
			/* find the end of the string */
			while (Buf[Lines][i]=='\0' && i>=0) i--;
			/* and replace all \0's in the rest of the string by spaces */
			while (i>=0)
			{
				if (Buf[Lines][i]=='\0' || Buf[Lines][i]=='\b')
					Buf[Lines][i]=' ';
				i--;
			}
		}
	}
	while (Buf[Lines][0] != INFO_TAG);	/* repeat until new node mark is found */


	/* added for simplifing two-line ismenu and isnote functs */
	if (Lines)
	{
		strcpy(Buf[Lines], "\n");
		Buf[Lines] = (char*)xrealloc(Buf[Lines], strlen(Buf[Lines]) + 1);
	}

	fseek(id, -2, SEEK_CUR);
#undef Type
#undef Buf
#undef Lines
}

void
load_indirect(char **message, long lines)
{
	for (long i = 1; i < lines; i++) { /* Avoid the last line.  (Why?) */
		string wsk_string = message[i];
		unsigned int n = 0;
		/* Find the first colon, but not in position 0 */
		n = wsk_string.find(':', 1);
		if (n == string::npos) {
			; /* No colon.  Invalid entry. */
		} else {
			Indirect my_entry;
			my_entry.filename = wsk_string.substr(0, n);
			string remainder = wsk_string.substr(n + 2, string::npos);
			my_entry.offset = atoi(remainder.c_str());
			indirect.push_back(my_entry);
		}
	}
}

void
load_tag_table(char **message, long lines)
{
	int is_indirect = 0;

	/*
	 * if in the first line there is a(indirect) string, skip that line
	 * by adding the value of is_indirect=1 to all message[line] references.
	 */
	if (strcasecmp("(Indirect)", message[1]) == 0)
		is_indirect = 1;
	tag_table.clear();

	for (long i = 1; i < lines - is_indirect; i++)
	{
		string wsk_string = message[i + is_indirect];
		/* Skip first character and nonwhitespace after it.
		 * (Why first character? FIXME) 
		 * plus one more (space) character */
		string::size_type j;
		for (j = 1; j < wsk_string.size(); j++) {
			if (isspace(wsk_string[j])) {
				j++;
				break;
			}
		}
		string trimmed = wsk_string.substr(j); /* Might be "" */
		if (trimmed == "") {
			continue;
		}

		/* Find INDIRECT_TAG character, but skip at least one character
		 * so the node name is nonempty */
		string::size_type ind_tag_idx = trimmed.find(INDIRECT_TAG, 1);
		if (ind_tag_idx == string::npos) {
			continue;
		}
		TagTable my_tag;
		my_tag.nodename.assign(trimmed, 0, ind_tag_idx);
		string offset_string = trimmed.substr(ind_tag_idx + 1);
		my_tag.offset = atoi(offset_string.c_str());
		tag_table.push_back(my_tag);
	}

	/* info should ALWAYS start at the 'Top' node, not at the first
	   mentioned node(vide ocaml.info) */
	for (typeof(tag_table.size()) i = 0; i < tag_table.size(); i++)
	{
		if (strcasecmp(tag_table[i].nodename.c_str(), "Top") == 0)
		{
			FirstNodeOffset = tag_table[i].offset;
			FirstNodeName = tag_table[i].nodename;
		}
	}
	sort_tag_table();
}

int
seek_indirect(FILE * id)
{
	int finito = 0;
	long seek_pos;
	int input;
	char *type = (char*)xmalloc(1024);
	fseek(id, 0, SEEK_SET);
	while (!finito)		/*
						 * scan through the file, searching for "indirect:"
						 * string in the type(header) line of node.
						 */
	{
		while ((input = fgetc(id)) != INFO_TAG)
			if (input == EOF)
			{
				if (type)
				{
					xfree(type);
					type = 0;
				}
				return 0;
			}
		seek_pos = ftell(id) - 2;
		fgetc(id);
		fgets(type, 1024, id);
		if (strncasecmp("Indirect:", type, strlen("Indirect:")) == 0)
		{
			finito = 1;
		}
	}
	xfree(type);
	type = 0;
	if (!curses_open)
	{
		printf(_("Searching for indirect done"));
		printf("\n");
	}
	else
	{
		attrset(bottomline);
		mvhline(maxy - 1, 0, ' ', maxx);
		mvaddstr(maxy - 1, 0, _("Searching for indirect done"));
		attrset(normal);
	}
	fseek(id, seek_pos, SEEK_SET);
	return 1;
}

/*
 * second arg for dumping out verbose debug info or not :)
 */
int
seek_tag_table(FILE * id,int quiet)
{
	int finito = 0;
	long seek_pos;
	int input;
	char *type = (char*)xmalloc(1024);
	fseek(id, 0, SEEK_SET);
	/*
	 * Scan through the file, searching for a string
	 * "Tag Table:" in the type(header) line of node.
	 */
	while (!finito)
	{
		while ((input = fgetc(id)) != INFO_TAG)
		{
			if (input == EOF)
			{
				if (!quiet)
				{
					if (!curses_open) {
						printf(_("Warning: could not find tag table"));
						printf("\n");
					} else
					{
						attrset(bottomline);
						mvhline(maxy - 1, 0, ' ', maxx);
						mvaddstr(maxy - 1, 0, _("Warning: could not find tag table"));
						attrset(normal);
					}
				}
				if (type)
				{
					xfree(type);
					type = 0;
				}
				return 2;
			}
		}
		seek_pos = ftell(id) - 2;
		while (fgetc(id) != '\n')
		{
			if (feof(id))
				break;
		}
		fgets(type, 1024, id);
		if (strncasecmp("Tag Table:", type, strlen("Tag Table:")) == 0)
		{
			finito = 1;
		}
	}
	xfree(type);
	type = 0;
	if (!curses_open)
		printf(_("Searching for tag table done\n"));
	else
	{
		attrset(bottomline);
		mvhline(maxy - 1, 0, ' ', maxx);
		mvaddstr(maxy - 1, 0, "Searching for tag table done");
		attrset(normal);
	}
	fseek(id, seek_pos, SEEK_SET);
	return 1;
}

FILE *
opendirfile(int number)
{
	FILE *id = NULL;
	string tmpfilename;
	int dir_found = 0;
	int dircount = 0;
	struct stat status;

	if (number == 0)		/* initialize tmp filename for file 1 */
	{
		if (tmpfilename1 != "")
		{
			unlink(tmpfilename1.c_str());	/* erase old tmpfile */
		}
		tmpfilename = tmpfilename1;	/* later we will refere only to tmp1 */
	}

	int *fileendentries = (int*)xmalloc(infopaths.size() * sizeof(int));
	/* go through all paths */
	for (typeof(infopaths.size()) i = 0; i < infopaths.size(); i++)	{ 
		int lang_found = 0;
		for (int k = 0; k <= 1; k++) { /* Two passes: with and without LANG */
			string bufstr;
			if (k == 0) {
				char* getenv_lang = getenv("LANG");
				/* If no LANG, skip this pass */
				if (getenv_lang == NULL)
					continue;
				bufstr = infopaths[i];
				bufstr += '/';
				bufstr += getenv_lang;
				bufstr += "/dir";
			} else { /* k == 1 */
				/* If we found one with LANG, skip this pass */
				if (lang_found)
					continue;
				bufstr = infopaths[i];
				bufstr += "/dir";
			}

			for (int j = 0; j < SuffixesNumber; j++) { /* go through all suffixes */
				string bufstr_with_suffix;
				bufstr_with_suffix = bufstr;
				bufstr_with_suffix += suffixes[j].suffix;

				id = fopen(bufstr_with_suffix.c_str(), "r");
				if (id != NULL) {
					fclose(id);
					string command_string = suffixes[j].command;
					command_string += " ";
					command_string += bufstr_with_suffix;
					command_string += ">> ";
					command_string += tmpfilename;
					system(command_string.c_str());
					lstat(tmpfilename.c_str(), &status);
					fileendentries[dircount] = status.st_size;
					dircount++;
					dir_found = 1;
					lang_found = 1;
				}
			}
		}
	}
	if (dir_found)
		id = fopen(tmpfilename.c_str(), "r");
	/*
	 * Filter the concatenated dir pages to exclude hidden parts of info
	 * entries
	 */
	if (id)
	{
		char *tmp;
		long filelen, i;
		int aswitch = 0;
		int firstswitch = 0;
		dircount = 0;

		fseek(id, 0, SEEK_END);
		filelen = ftell(id);

		tmp = (char*)xmalloc(filelen);
		fseek(id, 0, SEEK_SET);
		fread(tmp, 1, filelen, id);
		fclose(id);
		id = fopen(tmpfilename.c_str(), "w");
		for (i = 0; i < filelen; i++)
		{
			if (tmp[i] == INFO_TAG)
			{
				aswitch ^= 1;
				if (!firstswitch)
					fputc(tmp[i], id);
				firstswitch = 1;
			}
			else if ((aswitch) ||(!firstswitch))
				fputc(tmp[i], id);
			if (i + 1 == fileendentries[dircount])
			{
				if (aswitch != 0)
					aswitch = 0;
				dircount++;	/* the last dircount should fit to the end of filelen */
			}
		}
		fputc(INFO_TAG, id);
		fputc('\n', id);
		fclose(id);
		id = fopen(tmpfilename.c_str(), "r");
		xfree(tmp);

		xfree(fileendentries);
		return id;
	}
	xfree(fileendentries);
	return NULL;
}

/*
 * Note: openinfo is a function for reading info files, and putting
 * uncompressed content into a temporary filename.  For a flexibility, there
 * are two temporary files supported, i.e.  one for keeping opened info file,
 * and second for i.e. regexp search across info nodes, which are in other
 * info-subfiles.  The temporary file 1 is refrenced by number=0, and file 2 by
 * number=1 Openinfo by default first tries the path stored in
 * filenameprefix and then in the rest of userdefined paths.
 */
FILE *
openinfo(const string filename, int number)
{
	FILE *id = NULL;
	string tmpfilename;

	if (filename == "dir")
	{
		return opendirfile(number);
	}

	if (number == 0) { /* initialize tmp filename for file 1 */
		if (tmpfilename1 != "")
		{
			unlink(tmpfilename1.c_str());	/* erase old tmpfile */
		}
		tmpfilename = tmpfilename1;	/* later we will refere only to tmp1 */
	} else { /* initialize tmp filename for file 2 */
		if (tmpfilename2 != "")
		{
			unlink(tmpfilename2.c_str());	/* erase old tmpfile */
		}
		tmpfilename = tmpfilename2;	/* later we will refere only to tmp2 */
	}

	/* FIXME: signed/unsigned issues (sigh) */
	for (int i = -1; i < (int)infopaths.size(); i++) { /* go through all paths */
		string mybuf;
		if (i == -1) {
			/*
			 * no filenameprefix, we don't navigate around any specific
			 * infopage set, so simply scan all directories for a hit
			 */
			if (filenameprefix.empty())
				continue;
			else {
				mybuf = filenameprefix;
				mybuf += "/";
				string basename_string;
				basename(filename, basename_string);
				mybuf += basename_string;
			}
		} else {
			mybuf = infopaths[i];
			/* Modify mybuf in place by suffixing filename -- eeewww */
			int result = matchfile(mybuf, filename);
			if (result == 1)	/* no match found in this directory */
				continue;
		}
		for (int j = 0; j < SuffixesNumber; j++) { /* go through all suffixes */
			string buf_with_suffix = mybuf;
			buf_with_suffix += suffixes[j].suffix;
			id = fopen(buf_with_suffix.c_str(), "r");
			if (id) {
				fclose(id);
				/* Set global filenameprefix to the dirname of the found file */
				dirname(buf_with_suffix, filenameprefix);

				string command_string = suffixes[j].command;
				command_string += ' ';
				command_string += buf_with_suffix;
				command_string += "> ";
				command_string += tmpfilename;
				system(command_string.c_str());

				id = fopen(tmpfilename.c_str(), "r");
				if (id)
				{
					return id;
				}
			}
		}
		if ((i == -1) && ( !filenameprefix.empty() ))
			/* if we have a nonzero filename prefix,
				 that is we view a set of infopages,
				 we don't want to search for a page
				 in all directories, but only in
				 the prefix directory. Therefore
				 break here. */
			break;
	}
	return 0;
}

void
addrawpath(const string filename_string)
{
	/* Get the portion up to the last slash. */
	string dirstring;
	string::size_type index = filename_string.rfind('/');
	if (index != string::npos)
		dirstring = filename_string.substr(0, index + 1);
	else
		dirstring = "./"; /* If no directory part, use current directory */

	/* Add to beginning */
	infopaths.insert(infopaths.begin(), dirstring);
}

int
isininfopath(char *name)
{
	for (typeof(infopaths.size()) i = 0; i < infopaths.size(); i++)
	{
		if (infopaths[i] == name)
			return 1;		/* path already exists */
	}
	return 0;			/* path not found in previous links */
}

/* returns the number of chars ch in string str */
unsigned int
charcount(const char *str, const char ch)
{
	int num = 0;
	const char *c;

	c = str;

	while (*c != '\0')
	{
		if (*c++ == ch)
			num++;
	}
	return num;
}

/*
 * find the paths where info files are to be found,
 * and put them in the global var infopaths[]
 */
void
initpaths()
{
	vector<string> paths;
	char *rawlang = NULL;
	string lang;
	string langshort;
	int ret;
	size_t len;
	struct stat sbuf;

	/* first concat the paths */
	string infopath;
	char* env = getenv("INFOPATH");
	if (env != NULL)
	{
		infopath = env; 
	}
	infopath += ":"; /* FIXME: what if one of the two is blank? */
	infopath += configuredinfopath;

	/* split at ':' and put the path components into paths[] */
	string::size_type stop_idx;
	string::size_type start_idx = 0;
	do {
		stop_idx = infopath.find(':', start_idx);
		string dir;
		dir  = infopath.substr(start_idx, stop_idx - start_idx);
		/* if this actually is a non-empty string, add it to paths[] */
		if (dir.length() > 0) {
			paths.push_back(dir);
		}
		start_idx = stop_idx + 1;
	} while (stop_idx != string::npos) ;


	/* get the current $LANG, if any (to use for localized info pages) */
	rawlang = getenv("LANG");
	if (rawlang) {
		lang = rawlang;
		/* fix the lang string */
		/* cut off the charset */
		string::size_type idx = lang.find('.');
		if (idx != string::npos) {
			lang.resize(idx);
		}
		/* if lang is sublocalized (nl_BE or so), also use short version */
		idx = lang.find('_');
		if (idx != string::npos) {
			langshort = lang;
			langshort.resize(idx);
		}
	}

	/* if we have a LANG defined, add paths with this lang to the paths[] */
	if (lang != "") {
		typeof(paths.size()) old_size = paths.size();
		if (langshort != "") {
			paths.resize(old_size * 3);
		} else {
			paths.resize(old_size * 2);
		}
		for (typeof(paths.size()) i=0; i<old_size; i++) {
			string tmp;
			tmp = paths[i];
			tmp += '/';
			tmp += lang;
			/* add the lang specific dir at the beginning */
			paths[old_size+i] = paths[i];
			paths[i] = tmp;
			
			if (langshort != "") {
				string tmp;
				tmp = paths[i];
				tmp += '/';
				tmp += langshort;
				paths[2*old_size+i] = paths[old_size+i];
				paths[old_size+i] = tmp;
			}
		}
	}

#ifdef ___DEBUG___
	/* for debugging */
	for (typeof(paths.size()) i=0; i<paths.size(); i++)
		fprintf(stderr,"--> %s\n", paths[i].c_str());
#endif

	/* ok, now we have all the (possibly) revelevant paths in paths[] */
	/* now loop over them, see if they are valid and if they are duplicates*/
	vector<ino_t> inodes;
	int numpaths = 0;
	len = 0;
	for (typeof(paths.size()) i=0; i<paths.size(); i++) {
		inodes.push_back(0);
		/* stat() the dir */
		ret = stat( paths[i].c_str(), &sbuf);
		/* and see if it could be opened */
		if (ret < 0)
		{
#ifdef ___DEBUG___
			fprintf(stderr, "error while opening `%s': %s\n",
					paths[i].c_str(), strerror(errno) );
#endif
			paths[i] = "";
			inodes[i] = 0;
		}
		else
		{
			inodes[i] = sbuf.st_ino;
		}

		/* now check if this path is a duplicate */
		for (typeof(paths.size()) j=0; j<i; j++)
		{
			if (inodes[j]==inodes[i])
				paths[i] = "";
		}

		/* calculate the total number of vali paths and the size of teh strings */
		if (paths[i]!="")
		{
			numpaths++;
			len += paths[i].length() + 1;
		}
	}


	infopaths.clear();
	for (typeof(paths.size()) i=0; i<paths.size(); i++)
	{
		if (paths[i]!="")
		{
			infopaths.push_back(paths[i]);
		}
	}

#ifdef ___DEBUG___
	/* for debugging */
	fprintf(stderr, "%i valid info paths found:\n", infopaths.size());
	for (typeof(paths.size()) i=0; i<infopaths.size(); i++)
		if (infopaths[i] != "") fprintf(stderr,"--> %s\n", infopaths[i].c_str());
#endif
}

void
create_indirect_tag_table()
{
	FILE *id = 0;
	int initial;
	for (typeof(indirect.size()) i = 0; i < indirect.size(); i++)
	{
		id = openinfo(indirect[i].filename, 1);
		initial = tag_table.size(); /* Before create_tag_table operates */
		if (id)
		{
			create_tag_table(id);
			FirstNodeOffset = tag_table[0].offset;
			FirstNodeName = tag_table[0].nodename;
		}
		fclose(id);
		for (typeof(tag_table.size()) j = initial; j < tag_table.size(); j++)
		{
			tag_table[j].offset +=(indirect[i].offset - FirstNodeOffset);
		}
	}
	FirstNodeOffset = tag_table[0].offset;
	FirstNodeName = tag_table[0].nodename;
	sort_tag_table();
}

void
create_tag_table(FILE * id)
{
	char *buf = (char*)xmalloc(1024);
	long oldpos;
	fseek(id, 0, SEEK_SET);
	while (!feof(id))
	{
		if (fgetc(id) == INFO_TAG)	/* We've found a node entry! */
		{
			while (fgetc(id) != '\n');	/* skip '\n' */
			oldpos = ftell(id);	/* remember this file position! */
			/*
			 * it is a an eof-fake-node (in some info files it happens, that
			 * the eof'ish end of node is additionaly signalised by an INFO_TAG
			 * We give to such node an unlike to meet nodename.
			 */
			if (fgets(buf, 1024, id) == NULL) {
				TagTable my_tag;
				my_tag.nodename = "12#!@#4";
				my_tag.offset = 0;
				tag_table.push_back(my_tag);
			} else {
				int colons = 0, i, j;
				int buflen = strlen(buf);
				for (i = 0; i < buflen; i++)
				{
					if (buf[i] == ':')
						colons++;
					if (colons == 2)	/*
										 * the string after the second colon
										 * holds the name of current node.
										 * The name may then end with `.',
										 * or with a newline, which is scanned
										 * bellow.
										 */
					{
						for (j = i + 2; j < buflen; j++)
						{
							if ((buf[j] == ',') ||(buf[j] == '\n'))
							{
								TagTable my_tag;
								buf[j] = 0;
								buflen = j;
								my_tag.nodename = buf + i + 2;
								my_tag.offset = oldpos - 2;
								tag_table.push_back(my_tag);
								break;
							}
						}
						break;
					}
				}		/* end: for loop, looking for second colon */
			}			/* end: not a fake node */
		}			/* end: we've found a node entry(INFO_TAG) */
	}				/* end: global while loop, looping until eof */
	xfree(buf);
	buf = 0;
	if (indirect.empty()) /* originally (!indirect) -- check this NCN FIXME */
	{
		FirstNodeOffset = tag_table[0].offset;
		FirstNodeName = tag_table[0].nodename;
		sort_tag_table();
	}
}

void
seeknode(int tag_table_pos, FILE ** Id)
{
	int i;
#define id	(*Id)
	/*
	 * Indirect nodes are seeked using a formula:
	 * file-offset = tagtable_offset - indirect_offset +
	 *             + tagtable[1]_offset
	 */
	if (!(indirect.empty()))	/* Originally if (indirect) -- NCN CHECK FIXME */
	{
		for (i = indirect.size() - 1; i >= 0; i--)
		{
			if (indirect[i].offset <= tag_table[tag_table_pos].offset)
			{
				long off = tag_table[tag_table_pos].offset - indirect[i].offset + FirstNodeOffset - 4;
				fclose(id);
				id = openinfo(indirect[i].filename, 0);
				if (id == NULL)
				{
					closeprogram();
					printf(_("Error: could not open info file\n"));
					exit(1);
				}
				if (off > 0)
					fseek(id, off, SEEK_SET);
				else
					fseek(id, off, SEEK_SET);
				break;
			}
		}
	}
	else
	{
		long off = tag_table[tag_table_pos].offset - 4;
		if (off > 0)
			fseek(id, off, SEEK_SET);
		else
			fseek(id, off, SEEK_SET);
	}
#undef id
}

/*
 * Strip one trailing .gz, .bz2, etc.
 * Operates in place.
 */
void
strip_compression_suffix(string& filename)
{
	for (int j = 0; j < SuffixesNumber; j++)
	{
		string::size_type suffix_len =  strlen(suffixes[j].suffix);
		if (suffix_len == 0) {
			/* Nothing is a suffix, but that gives an early false positive. */
			continue;
		}
		if (    (filename.length() >= suffix_len)
		     && (filename.compare(filename.length() - suffix_len,
		                          suffix_len, suffixes[j].suffix) == 0)
		   ) {
			/* Truncate string. */
			filename.resize(filename.length() - suffix_len);
			break;
		}
	}
}

