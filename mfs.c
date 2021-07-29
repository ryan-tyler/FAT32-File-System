/*

	Name: Ryan Tyler

*/

// The MIT License (MIT)
//
// Copyright (c) 2016, 2017 Trevor Bakker
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 5     // Mav shell only supports five arguments

FILE *fp;
FILE *newdirect;

char BS_OEMName[8];
uint16_t BPB_BytsPerSec;
uint8_t BPB_SecPerClus;
uint16_t BPB_RsvdSecCnt;
uint8_t BPB_NumFATs;
uint16_t BPB_RootEntCnt;
char BPB_VolLab[11];
uint32_t BPB_FATSz32;

// first cluster of the root directory on FAT32
uint32_t BPB_RootClus;

uint32_t RootDirSectors = 0; // this number is always 0 on FAT32
uint32_t FirstDataSector = 0;
uint32_t FirstSectorofCluster = 0;

uint32_t root_directory_address; //= (BPB_NumFATs * BPB_FATSz32 * BPB_BytsPerSec) + (BPB_RsvdSecCnt * BPB_BytsPerSec);
uint32_t current_directory;

uint16_t NextLB ( uint32_t sector )
{
	uint32_t FATAddress = ( BPB_BytsPerSec * BPB_RsvdSecCnt ) + ( sector * 4 );
	uint16_t value;
	fseek( fp, FATAddress, SEEK_SET );
	fread( &value, 2, 1, fp );
	return value;
}

struct __attribute__((__packed__)) DirectoryEntry {
	/* data */
	char DIR_Name[11];              // 11 bytes
	uint8_t DIR_Attr; 							// 1 byte
	uint8_t Unused1[8]; 						// 8 bytes
	uint16_t DIR_FirstClusterHigh;  // 2 bytes
	uint8_t Unused2[4]; 						// 4 bytes
	uint16_t DIR_FirstClusterLow;   // 2 bytes
	uint32_t DIR_FileSize;					// 4 bytes
}; // 32 total

// will read in root at the open of image
struct DirectoryEntry root_dir[16];
struct DirectoryEntry currentDirectory[16];

int LBAtoOffset ( uint32_t sector )
{
	return ((sector-2) * BPB_BytsPerSec) + (BPB_BytsPerSec * BPB_RsvdSecCnt) +
		(BPB_NumFATs * BPB_FATSz32 * BPB_BytsPerSec);
}

void seek (FILE *filePointer, int offset)
{
	fseek(filePointer,offset,SEEK_SET);
}

FILE* open_image (FILE *filePointer, char* tokenPointer)
{
	if ( filePointer != NULL )
	{
		printf("Error: File system image already open.\n");
	}

	if ( tokenPointer == NULL )
	{
		printf("Usage: open <filename>\n");
	}

	filePointer = fopen(tokenPointer,"r");

	if ( (filePointer == NULL && tokenPointer != NULL) ) // || strcmp(tokenPointer,"fat32.img" ) != 0
	{
		printf("Error: File system image not found.\n");
	}

	return filePointer;
}

FILE* close_image ( FILE *filePointer )
{
	if ( fp == NULL )
	{
		printf("Error: File system must be opened first.\n");
	}
	fclose(filePointer);
	filePointer = NULL;
	return filePointer;
}

FILE* get_image_info ( FILE* filePointer ) //void get_image_info ()
{
	//
	fseek(filePointer,11,SEEK_SET);
	fread(&BPB_BytsPerSec,2,1,filePointer);
	//
	fseek(filePointer,14,SEEK_SET);
	fread(&BPB_RsvdSecCnt,2,1,filePointer);
	//
	fseek(filePointer,13,SEEK_SET);
	fread(&BPB_SecPerClus,1,1,filePointer);
	//
	fseek(filePointer,16,SEEK_SET);
	fread(&BPB_NumFATs,1,1,filePointer);
	//
	fseek(filePointer,36,SEEK_SET);
	fread(&BPB_FATSz32,4,1,filePointer);
	//
	return filePointer;
}

void get_location()
{
	fseek(fp,current_directory,SEEK_SET);
}

int main(int argc, char* argv[])
{
	char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );
	int x = 1;


  while( x )
  {
    // print "mfs" prompt
    printf ("mfs> ");

    // Read the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );


    char *token[MAX_NUM_ARGUMENTS];

    int token_count = 0;

    // Pointer to point to the token
    // parsed by strsep
    char *arg_ptr;

    char *working_str  = strdup( cmd_str );

		// we are going to move the working_str pointer so
    // keep track of its original value so we can deallocate
    // the correct amount at the end
    char *working_root = working_str;

    // Tokenize the input stringswith whitespace used as the delimiter
    while ( ( (arg_ptr = strsep(&working_str, WHITESPACE ) ) != NULL) &&
    (token_count<MAX_NUM_ARGUMENTS))
    {
			token[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );

      if( strlen( token[token_count] ) == 0 )
      {
      	token[token_count] = NULL;
      }
      token_count++;
    }

    // user types 'exit' or 'quit' to leave program
    if ( strcmp (token[0], "exit") == 0 || strcmp (token[0], "quit") == 0 ||
				strcmp (token[0], "EXIT") == 0	|| strcmp (token[0], "QUIT") == 0)
		{
			x = 0;
		}

		// opening FAT32 image
		else if ( strcmp (token[0], "open") == 0 )
		{
			fp = open_image(fp,token[1]);
			fseek(fp,11,SEEK_SET);
			fread(&BPB_BytsPerSec,2,1,fp);
			//
			fseek(fp,14,SEEK_SET);
			fread(&BPB_RsvdSecCnt,2,1,fp);
			//
			fseek(fp,13,SEEK_SET);
			fread(&BPB_SecPerClus,1,1,fp);
			//
			fseek(fp,16,SEEK_SET);
			fread(&BPB_NumFATs,1,1,fp);
			//
			fseek(fp,36,SEEK_SET);
			fread(&BPB_FATSz32,4,1,fp);

			// past boot, arrived at FAT, move fp towards directories

			root_directory_address = (BPB_NumFATs * BPB_FATSz32 * BPB_BytsPerSec) +
															(BPB_RsvdSecCnt * BPB_BytsPerSec);

			fseek(fp,root_directory_address,SEEK_SET);
		}

		// close FAT32 image
		else if ( strcmp (token[0], "close") == 0 )
		{
			fp = close_image(fp);
		}

		// 'info'
		else if ( strcmp (token[0], "info") == 0 )
		{
			// check to make sure FAT32 image was actually opened by user
			if ( fp == NULL )
			{
				printf("Error: File system image must be opened first.\n");
			 }
			else
			{
				printf("BPB_BytsPerSec: %d %x\n", BPB_BytsPerSec,BPB_BytsPerSec);

				printf("BPB_RsvdSecCnt: %d %x\n", BPB_RsvdSecCnt,BPB_RsvdSecCnt);

				printf("BPB_SecPerClus: %d %x\n", BPB_SecPerClus,BPB_SecPerClus);

				printf("BPB_NumFATs: %d %x\n", BPB_NumFATs,BPB_NumFATs);

				printf("BPB_FATSz32: %d %x\n", BPB_FATSz32,BPB_FATSz32);
			}
			current_directory = root_directory_address;
			fseek(fp,current_directory,SEEK_SET);
		}

		// 'stat' <filename> or <directory name>

		else if ( strcmp (token[0], "stat") == 0 )
		{
			if ( fp == NULL )
			{
				printf("Error: File system image must be opened first.\n");
			}
			// check if user actually entered a second argument in command line
			if ( token[1] == NULL )
			{
				printf("Error: must enter filename or directory name.\n");
			}
				// moves to start of clusters
				// root directory located here
				// character array will hold FAT32 approved image filename
				char expanded_name[12];
				//char* pointer = expanded_name;
				memset( expanded_name, ' ', 12 );
				// tokenize user input for file/directory name
				char *newtok = strtok( token[1], "." );

				strncpy( expanded_name, newtok, strlen( newtok ) );

				newtok = strtok( NULL, "." );

				if( newtok )
				{
					strncpy( (char*)(expanded_name+8), newtok, strlen(newtok) );
				}
				// change newline character to null character
				expanded_name[11] = '\0';
				// changing file/directory to uppercase
				int i;
				for( i = 0; i < 11; i++ )
				{
					expanded_name[i] = toupper( expanded_name[i] );
				}
				fseek(fp,root_directory_address,SEEK_SET);
				for ( i = 0; i < 16; i++ )
				{
					// read first file name in current directory
					fread(&currentDirectory[i].DIR_Name,11,1,fp);
					// check to see if file/directory is what user is looking for
					if ( strcpy(expanded_name,
													currentDirectory[i].DIR_Name) == 0 )
					{
						// grab attribute; print
						fread(&currentDirectory[i].DIR_Attr,1,1,fp);
						printf("Attribute: 0x%x, ",currentDirectory[i].DIR_Attr);
						// seek past 'unused' 8 bytes
						fseek(fp,8,SEEK_CUR);
						fread(&currentDirectory[i].DIR_FirstClusterHigh,2,1,fp);
						printf("Starting cluster: 0x%x\n",currentDirectory[i].DIR_FirstClusterHigh);
						// seek to next file/directory entry in currenty directory
						fseek(fp,12,SEEK_CUR);
					}
					else
					{
						// file doesn't match; seek to next file in directory
						fseek(fp,21,SEEK_CUR);
					}

				}

		}

		// 'get' <filename>
		else if ( strcmp (token[0], "get") == 0 )
		{
			// fread(), 1st argument is where the read will store
			if ( fp == NULL )
			{
				printf("Error: File system image must be opened first.\n");
			}
		}

		// 'put' <filename>
		else if ( strcmp (token[0], "put") == 0 )
		{
			if ( fp == NULL )
			{
				printf("Error: File system image must be opened first.\n");
			}
		}

		// 'cd' <directory>
		// will change directory of whatever the location is currently
		else if ( strcmp (token[0], "cd") == 0 )
		{
			if ( fp == NULL )
			{
				printf("Error: File system image must be opened first.\n");
			}

			else
			{
				char expanded_name[12];
				//char* pointer = expanded_name;
				memset( expanded_name, ' ', 12 );
				// tokenize user input for file/directory name
				char *newtok = strtok( token[1], "." );

				strncpy( expanded_name, newtok, strlen( newtok ) );

				newtok = strtok( NULL, "." );

				if( newtok )
				{
					strncpy( (char*)(expanded_name+8), newtok, strlen(newtok) );
				}
				// change newline character to null character
				expanded_name[11] = '\0';
				// changing file/directory to uppercase
				int i;
				for( i = 0; i < 11; i++ )
				{
					expanded_name[i] = toupper( expanded_name[i] );
				}
				fseek(fp,root_directory_address,SEEK_SET);
				//int i;
				for ( i = 0; i < 16; i++ )
				{
					// read first file name in current directory
					fread(&currentDirectory[i].DIR_Name,11,1,fp);
					// check to see if file/directory is what user is looking for
					if ( strcmp(expanded_name,
													currentDirectory[i].DIR_Name) == 0 )
					{
						// match found
						// grab low cluster number
						// seek to low cluster
						fseek(fp,15,SEEK_CUR);
						// read in low cluster number to directory struct
						fread(&currentDirectory[i].DIR_FirstClusterLow,2,1,fp);
						uint32_t new_dir_offset = LBAtoOffset(currentDirectory[i].DIR_FirstClusterLow);
						fseek(fp,new_dir_offset,SEEK_SET);
					}
					else
					{
						// file doesn't match; seek to next file in directory
						fseek(fp,21,SEEK_CUR);
					}
				}
			}
		}

		else if ( strcmp (token[0], "ls") == 0 )
		{

			if ( fp == NULL )
			{
				printf("Error: File system image must be opened first.\n");
			}

			else
			{
				int i;
				for ( i = 0; i < 16; i++ )
				{
					// you now have the entire directory in each
					// element of the directory array
					fread(&currentDirectory[i].DIR_Name,11,1,fp);
					fread(&currentDirectory[i].DIR_Attr,1,1,fp);

					if ( currentDirectory[i].DIR_Attr == 0x01 ||
							currentDirectory[i].DIR_Attr == 0x10 ||
							currentDirectory[i].DIR_Attr == 0x20 )
						{
							printf("%.11s\n",currentDirectory[i].DIR_Name);
						}

					//printf("%.11s\n",root_dir[i].DIR_Name);
					fseek(fp,20,SEEK_CUR);
				}
			}

		}

		// 'read' <filename> <position> <number of bytes>
		else if ( strcmp (token[0], "read") == 0 )
		{
			if ( fp == NULL )
			{
				printf("Error: File system image must be opened first.\n");
			}

			// position - fseek to user defined position of file
			// number of bytes - how long the file is read
			//struct
			else
			{

				char expanded_name[12];
				//char* pointer = expanded_name;
				memset( expanded_name, ' ', 12 );
				// tokenize user input for file/directory name
				char *newtok = strtok( token[1], "." );

				strncpy( expanded_name, newtok, strlen( newtok ) );

				newtok = strtok( NULL, "." );

				if( newtok )
				{
					strncpy( (char*)(expanded_name+8), newtok, strlen(newtok) );
				}
				// change newline character to null character
				expanded_name[11] = '\0';
				// changing file/directory to uppercase
				int i;
				for( i = 0; i < 11; i++ )
				{
					expanded_name[i] = toupper( expanded_name[i] );
				}
				fseek(fp,root_directory_address,SEEK_SET);

				for ( i = 0; i < 16; i++ )
				{
					// read first file name in current directory
					fread(&currentDirectory[i].DIR_Name,11,1,fp);
					// check to see if file/directory is what user is looking for
					if ( strcmp(expanded_name,
													currentDirectory[i].DIR_Name) == 0 )
					{

					}
					else
					{
						// file doesn't match; seek to next file in directory
						fseek(fp,21,SEEK_CUR);
					}
				}
			}

		}

		else
		{
			printf("Error. Possible commands: <open> <filename>, <close>, <info>\n");
		}

    free( working_root );
  }
  	fclose(fp);

  	return 0;
}
