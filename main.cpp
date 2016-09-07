#include <iostream>
using namespace std;

struct entry{
	short size;
	char type;
	char name[9];
	int link;
};

struct file{
	int back;
	int fwrd;
	char data[504];
};

struct dir{
	int back;				//Block number of the first block of the directory
	int fwrd;				//Block number of the next block of the directory.  < 0 if no other block exists.
	int free;				//Index of the first free spot in listing of the current block.
	int size;				//Number of blocks allocated to the directory.
	entry listing[31];
};

union sector{
	char arr[512];
	file file;
	dir dir;
};

//Globals
bool run = true;
sector disk[100];
bool FSL[100];

sector local;
int PSec;
int FSec;
int Flist;
int fState = 0;			//0 = closed, 1 = input, 2 = output, 3 = update	
int written = 0;
bool dirty = false;
char *ptr = NULL;

//Disk Functions
void DREAD(int num, sector &sec){
	memcpy(sec.arr, disk[num].arr, 512);
}

void DWRITE(int num, sector &sec){
	memcpy(disk[num].arr, sec.arr, 512);
}

//File System Functions
void _create(char* _param){
	if (fState != 0){
		cout << "A File is open. Please close file before attempting to create." << endl;
		return;
	}
	//Find free sector
	int freeSec = 1;				// Free Sector number
	for (; freeSec < 100; freeSec++){
		if (FSL[freeSec] == false){
			break;
		}
	}
	if (FSL[freeSec] == true){
		cout << "Disk is full. Cannot Allocate Sector." << endl;
		return;
	}

	//local copy of params
	char param[100];		//Copy of the param for editing and use in strtok.
	strcpy(param, _param);

	//Tokenize parameters
	char* type;				//type parameter
	char* path[10];			//Path or file name;
	int p = -1;				// Number of parent directories. If 1, then root is parent directory.

	type = strtok(param, " ");
	do{
		p++;
		path[p] = strtok(NULL, "/ ");
	} while (path[p] != NULL);

	int pathIdx = 0;
	int sec = 0;
	int found = -1;

	//Find final parent directory
	while (p > 1){
		DREAD(sec, local);
		int numBlocks = local.dir.size;		//If the dir extend to more than one block
		// This loops within one dir in all its blocks
		while (numBlocks > 0){
			//Just because a dir has more than one block does not mean the first blocks do not have space.
			for (int i = 0; i < 31; i++){
				if (local.dir.listing[i].type == 'd' && (strcmp(local.dir.listing[i].name, path[pathIdx]) == 0)){
					found = i;
					break; // out of current block of the dir
				}
			}
			//Will drop out of searching inside the current dir because the next path has been found.
			if (found != -1){
				break;
			}
			//If we did not find the path in the current block
			else{
				//Decrement the remaining number of blocks to be searched.
				numBlocks--;
				//If there are no other blocks then exit. Incorrect path was provided.
				if (numBlocks == 0){
					cout << path[pathIdx] << " does not exist. Invalid Path." << endl;
					return;
				}
				//If there are other blocks load the other blocks.
				else{
					sec = local.dir.fwrd;
					DREAD(sec, local);
				}
			}
		} //End one dir search

		//load the next dir to search or if p == 1 then this loop will terminate
		sec = local.dir.listing[found].link;
		p--;			// decrement the number of paths left to follow
		pathIdx++;		// The next path that must be found or the name of the file/dir
	}// End loop. I found the correct dir, to create the new dir/file.

	//load parent directory into local memory
	DREAD(sec, local);

	//Find Duplicate
	int dupe = sec;
	int numBlocks = local.dir.size;
	found = -1;
	// This loops within one dir in all its blocks
	while (numBlocks > 0){
		// Check the listings in current block for duplicates only based on allocation and name. 
		for (int i = 0; i < 31; i++){
			if (local.dir.listing[found].type != 'f' && (strcmp(local.dir.listing[i].name, path[pathIdx]) == 0)){
				found = i;
				break; // Break out when found
			}
		}
		// Stop searching if found.
		if (found != -1){
			break;
		}
		// If not found in the current block
		else{
			// Decrement the number of blocks to search
			numBlocks--;
			//If there are none left then duplicate does not exist.
			if (numBlocks == 0){
				break;
			}
			// If there are other blocks to search load the next one into memory.
			else{
				dupe = local.dir.fwrd;
				DREAD(dupe, local);
			}
		}
	}

	//Delete the dupe if found
	if (found != -1){
		dupe = local.dir.listing[found].link;
		int count = 1;
		int back = dupe;
		int curr = dupe;
		//If the dupe is a directory then we must check if its empty before deleting.
		if (local.dir.listing[found].type == 'd'){
			DREAD(dupe, local);
			do{
				if (local.dir.fwrd != -1){
					count++;
					curr = local.dir.fwrd;
					DREAD(local.dir.fwrd, local);
					back = local.dir.back;
				}
				else{
					if (local.dir.free < 31){
						cout << "Directory is not empty. Cannot delete Directory." << endl;
						return;
					}
					count--;
					FSL[curr] = 0;
					curr = back;
					DREAD(back, local);
					back = local.dir.back;
					local.dir.fwrd = -1;
				}
			} while (count != 0);
		}
		else{
			DREAD(dupe, local);
			do{
				if (local.file.fwrd != -1){
					count++;
					curr = local.file.fwrd;
					DREAD(local.file.fwrd, local);
					back = local.file.back;
				}
				else{
					count--;

					FSL[curr] = 0;
					curr = back;
					DREAD(back, local);
					back = local.file.back;
					local.file.fwrd = -1;
				}
			} while (count != 0);
		}
		DREAD(sec, local);
		local.dir.free++;
		local.dir.listing[found].type = 'f';
		DWRITE(sec, local);
	}

	//Search for space in parent directory for new dir/file
	int block = sec;
	found = -1;
	do{
		DREAD(block, local);
		for (int i = 0; i < 31; i++){
			if (local.dir.listing[i].type == 'f'){
				found = i;
				break;
			}
		}
		if (found != -1){
			break;
		}
		else{
			block = local.dir.fwrd;
		}
	} while (block != -1);

	sec = block;

	if (found == -1){
		local.dir.fwrd = freeSec;
		local.dir.size += 1;
		FSL[freeSec] = true;
		int size = local.dir.size;
		DREAD(freeSec, local);
		local.dir.back = sec;
		local.dir.fwrd = -1;
		local.dir.free = 31;
		local.dir.size = size;

		sec = freeSec;
		DWRITE(sec, local);

		freeSec = 1;
		for (; freeSec < 100; freeSec++){
			if (FSL[freeSec] == false){
				break;
			}
		}
		if (FSL[freeSec] == true){
			cout << "Disk is full. Cannot Allocate Sector." << endl;
			return;
		}
	}

	FSL[freeSec] = true;
	local.dir.free--;

	strcpy(local.dir.listing[found].name, path[pathIdx]);
	local.dir.listing[found].link = freeSec;
	local.dir.listing[found].size = 0;
	local.dir.listing[found].type = type[0];

	DWRITE(sec, local);
	PSec = sec;

	DREAD(freeSec, local);
	FSec = freeSec;
	Flist = found;

	if (type[0] == 'd'){
		local.dir.back = PSec;
		local.dir.fwrd = -1;
		local.dir.free = 31;
		local.dir.size = 1;
		for (int i = 0; i < 31; i++){
			local.dir.listing[i].type = 'f';
		}
		DWRITE(FSec, local);
		fState = 0;
	}
	else{
		local.file.back = -1;
		local.file.fwrd = -1;
		fState = 2;
		for (int i = 0; i < 504; i++){
			local.file.data[i] = '\0';
		}
		ptr = local.file.data;
		DWRITE(FSec, local);
	}

};
void _open(char* _param){
	if (fState != 0){
		cout << "A File is open. Please close file before attempting to open another." << endl;
		return;
	}
	//local copy of params
	char param[100];		//Copy of the param for editing and use in strtok.
	strcpy(param, _param);

	//Tokenize parameters
	char* type;				//type parameter
	char* path[10];			//Path or file name;
	int p = -1;				// Number of parent directories. If 1, then root is parent directory.

	type = strtok(param, " ");

	do{
		p++;
		path[p] = strtok(NULL, "/ ");
	} while (path[p] != NULL);

	int pathIdx = 0;
	int sec = 0;
	int found = -1;

	while (p > 1){
		DREAD(sec, local);
		int numBlocks = local.dir.size;
		while (numBlocks > 0){
			for (int i = 0; i < 31; i++){
				if (local.dir.listing[i].type == 'd' && (strcmp(local.dir.listing[i].name, path[pathIdx]) == 0)){
					found = i;
					break;
				}
			}
			if (found != -1){
				break;
			}
			else{
				numBlocks--;
				if (numBlocks == 0){
					cout << path[pathIdx] << " does not exist. Invalid Path." << endl;
					return;
				}
				else{
					sec = local.dir.fwrd;
					DREAD(sec, local);
				}
			}
		}

		sec = local.dir.listing[found].link;
		p--;
		pathIdx++;
	}

	DREAD(sec, local);

	int numBlocks = local.dir.size;
	found = -1;
	while (numBlocks > 0){
		for (int i = 0; i < 31; i++){
			if (local.dir.listing[i].type == 'u' && (strcmp(local.dir.listing[i].name, path[pathIdx]) == 0)){
				found = i;
				break;
			}
		}
		if (found != -1){
			break;
		}
		else{
			numBlocks--;
			if (numBlocks == 0){
				cout << path[pathIdx] << " does not exist." << endl;
				return;
			}
			else{
				sec = local.dir.fwrd;
				DREAD(sec, local);
			}
		}
	}
	PSec = sec;
	FSec = local.dir.listing[found].link;
	Flist = found;
	DREAD(FSec, local);
	fState = true;

	switch (tolower(type[0])){
	case 'i':
		fState = 1;
		ptr = &local.file.data[0];
		break;
	case 'o':
		fState = 2;
		ptr = &local.file.data[0];
		while (*ptr != '\0' || ptr <= &local.file.data[503]){
			ptr++;
		}
		break;
	case 'u':
		fState = 3;
		ptr = &local.file.data[0];
		break;
	default:
		cout << "Incorrect type, please use (I)nput, (O)utput, (U)pdate only." << endl;
		return;
	}
};
void _close(){
	if (fState == 0){
		cout << "A file is not open." << endl;
		return;
	}
	if (dirty){
		dirty = false;
		DWRITE(FSec, local);
		DREAD(PSec, local);
		local.dir.listing[Flist].size = ((written >= 504) && (written % 504 == 0)) ? 504 : (written % 504);
		DWRITE(PSec, local);
		written = 0;
	}
	fState = 0;
};
void _delete(char* _param){
	if (fState != 0){
		cout << "A File is open. Please close file before attempting to delete." << endl;
		return;
	}
	//local copy of params
	char param[80];		//Copy of the param for editing and use in strtok.
	strcpy(param, _param);

	//Tokenize parameters
	char* path[10];			//Path or file name;
	int p = 0;				// Number of parent directories. If 1, then root is parent directory.

	path[0] = strtok(param, "/ ");
	do{
		p++;
		path[p] = strtok(NULL, "/ ");
	} while (path[p] != NULL);

	int pathIdx = 0;
	int sec = 0;
	int found = -1;

	//Find final parent directory
	while (p > 1){
		DREAD(sec, local);
		int numBlocks = local.dir.size;		//If the dir extend to more than one block
		// This loops within one dir in all its blocks
		while (numBlocks > 0){
			//Just because a dir has more than one block does not mean the first blocks do not have space.
			for (int i = 0; i < 31; i++){
				if (local.dir.listing[i].type == 'd' && (strcmp(local.dir.listing[i].name, path[pathIdx]) == 0)){
					found = i;
					break; // out of current block of the dir
				}
			}
			//Will drop out of searching inside the current dir because the next path has been found.
			if (found != -1){
				break;
			}
			//If we did not find the path in the current block
			else{
				//Decrement the remaining number of blocks to be searched.
				numBlocks--;
				//If there are no other blocks then exit. Incorrect path was provided.
				if (numBlocks == 0){
					cout << path[pathIdx] << " does not exist. Invalid Path." << endl;
					return;
				}
				//If there are other blocks load the other blocks.
				else{
					sec = local.dir.fwrd;
					DREAD(sec, local);
				}
			}
		} //End one dir search

		//load the next dir to search or if p == 1 then this loop will terminate
		sec = local.dir.listing[found].link;
		p--;			// decrement the number of paths left to follow
		pathIdx++;		// The next path that must be found or the name of the file/dir
	}// End loop. I found the correct dir, to create the new dir/file.

	//load parent directory into local memory
	DREAD(sec, local);

	//Find Duplicate
	int dupe = sec;
	int numBlocks = local.dir.size;
	found = -1;
	// This loops within one dir in all its blocks
	while (numBlocks > 0){
		// Check the listings in current block for duplicates only based on allocation and name. 
		for (int i = 0; i < 31; i++){
			if (local.dir.listing[found].type != 'f' && (strcmp(local.dir.listing[i].name, path[pathIdx]) == 0)){
				found = i;
				break; // Break out when found
			}
		}
		// Stop searching if found.
		if (found != -1){
			break;
		}
		// If not found in the current block
		else{
			// Decrement the number of blocks to search
			numBlocks--;
			//If there are none left then duplicate does not exist.
			if (numBlocks == 0){
				cout << "File does not exist." << endl;
				return;
			}
			// If there are other blocks to search load the next one into memory.
			else{
				dupe = local.dir.fwrd;
				DREAD(dupe, local);
			}
		}
	}

	//Delete the dupe if found
	if (found != -1){
		dupe = local.dir.listing[found].link;
		int count = 1;
		int back = dupe;
		int curr = dupe;
		//If the dupe is a directory then we must check if its empty before deleting.
		if (local.dir.listing[found].type == 'd'){
			DREAD(dupe, local);
			do{
				if (local.dir.fwrd != -1){
					count++;
					curr = local.dir.fwrd;
					DREAD(local.dir.fwrd, local);
					back = local.dir.back;
				}
				else{
					if (local.dir.free < 31){
						cout << "Directory is not empty. Cannot delete Directory." << endl;
						return;
					}
					count--;
					FSL[curr] = 0;
					curr = back;
					DREAD(back, local);
					back = local.dir.back;
					local.dir.fwrd = -1;
				}
			} while (count != 0);
		}
		else{
			DREAD(dupe, local);
			do{
				if (local.file.fwrd != -1){
					count++;
					curr = local.file.fwrd;
					DREAD(local.file.fwrd, local);
					back = local.file.back;
				}
				else{
					count--;
					FSL[curr] = 0;
					curr = back;
					DREAD(back, local);
					back = local.file.back;
					local.file.fwrd = -1;
				}
			} while (count != 0);
		}
		DREAD(sec, local);
		local.dir.free++;
		local.dir.listing[found].type = 'f';
		DWRITE(sec, local);
	}
};
void _read(char* _param){
	if (fState == 2){
		cout << "File is not open in proper orientation. Please close file and use (U)pdate or (I)nput to open." << endl;
		return;
	}
	//Tokenize parameters
	int n = atoi(_param);			//Path or file name;

	for (int i = 0; i < n; i++){
		if (n > 504 && (ptr > &local.file.data[503])){
			if (local.file.fwrd != -1){
				FSec = local.file.fwrd;
				DREAD(FSec, local);
				ptr = local.file.data;
			}
		}
		char temp = *ptr;
		if (temp == '\0'){
			cout << "\nEnd of File.";
			break;
		}
		cout << *ptr;
		ptr = ptr++;
	}
	cout << endl;
}
void _write(char* _param){
	if (fState == 1){
		cout << "File is not open in proper orientation. Please close file and use (U)pdate or (O)utput to open." << endl;
		return;
	}
	DREAD(FSec, local);
	dirty = true;

	int len = strlen(_param);
	char* param = (char *)malloc(len);
	strcpy(param, _param);

	//Tokenize parameters
	int n = atoi(strtok(param, " '"));				//n parameter
	char* path = strtok(NULL, "'");				//Path or file name;
	len = strlen(path);

	int freeSecArr[99];
	int fsaLen = 0;
	int fsaLoc = 0;
	int temp = n / 504;
	if (temp){
		for (int i = 0; i < temp; i++){
			bool found = false;
			for (int j = 0; j < 100; j++){
				if (FSL[j] == false){
					freeSecArr[fsaLen] = j;
					FSL[j] = true;
					fsaLen++;
					found = true;
					break;
				}
			}
			if (!found)
				cout << "Not enought space avaliable on disk. " << (n - 504 - (fsaLen * 504)) << " bytes have not been written." << endl;
		}
	}

	while (written < n){
		if ((fsaLen != fsaLoc) && (ptr > &local.file.data[503])){
			local.file.fwrd = freeSecArr[fsaLoc];
			DWRITE(FSec, local);
			DREAD(freeSecArr[fsaLoc], local);
			local.file.back = FSec;
			local.file.fwrd = -1;
			ptr = local.file.data;
			FSec = freeSecArr[fsaLoc];
			fsaLoc++;
		}
		if (written >= len){
			*ptr = ' ';
			ptr++;
			written++;
		}
		else{
			*ptr = *path;
			ptr++;
			path++;
			written++;
		}
	}
	cout << "End of n bytes. " << written << " bytes written." << endl;
};
void _seek(char* _param){
	if (fState == 2){
		cout << "File is not open in proper orientation. Please close file and use (U)pdate or (I)nput to open." << endl;
		return;
	}
	//local copy of params
	char param[16];		//Copy of the param for editing and use in strtok.
	strcpy(param, _param);

	//Tokenize parameters
	int base = atoi(strtok(param, " "));
	int offset = atoi(strtok(NULL, " "));

	switch (base){
	case -1:
		//Find Start of File
		while (local.file.back != -1){
			DREAD(local.file.back, local);
		}
		//If Offset is less than start
		if (offset < 0){
			cout << "Invalid offset value." << endl;
			ptr = local.file.data;
			return;
		}
		//If Offset is within one block
		else if (offset < 504){
			ptr = local.file.data + offset;
		}
		//If Offset exceeds 1 block
		else{
			//If No other block exists
			if (local.file.fwrd == -1){
				cout << "Invalid offset value." << endl;
				ptr = local.file.data;
				return;
			}
			else{
				while (offset > 504){
					//If Blocks exist move forward
					if (local.file.fwrd != -1){
						DREAD(local.file.fwrd, local);
						offset -= 504;
					}
					//If Blocks don't exist
					else if ((local.file.fwrd == -1) && (offset > 503)){
						cout << "Invalid offset value." << endl;
						while (local.file.back != -1){
							DREAD(local.file.back, local);
						}
						ptr = local.file.data;
						return;
					}
				}
				ptr = local.file.data + offset;
			}
		}
		break;
	case 0:
		//If Offset is within one block
		if ((ptr + offset >= &local.file.data[0]) && (ptr + offset <= &local.file.data[503])){
			ptr = local.file.data + offset;
		}
		//If Offset is less than start of block
		else if (offset < 0){
			//If no other previous blocks
			if (local.file.back == -1){
				cout << "Invalid offset value." << endl;
				return;
			}
			else{
				while (offset < 0){
					//If Blocks exist move back
					if (local.file.back != -1){
						DREAD(local.file.back, local);
						offset += 504;
					}
					//If Blocks don't exist and we are not at a valid offset
					else if ((local.file.back == -1) && (offset < 0)){
						cout << "Invalid offset value." << endl;
						return;
					}
				}
				ptr = ptr + offset;
			}
		}
		
		//If Offset exceeds 1 block
		else{
			//If No other block exists
			if (local.file.fwrd == -1){
				cout << "Invalid offset value." << endl;
				ptr = local.file.data;
				return;
			}
			else{
				while (offset > 504){
					//If Blocks exist move forward
					if (local.file.fwrd != -1){
						DREAD(local.file.fwrd, local);
						offset -= 504;
					}
					//If Blocks don't exist
					else if ((local.file.fwrd == -1) && (offset > 504)){
						cout << "Invalid offset value." << endl;
						while (local.file.back != -1){
							DREAD(local.file.back, local);
						}
						ptr = local.file.data;
						return;
					}
				}
				ptr = ptr + offset;
			}
		}
		break;
	case 1:
		//Find End of File
		while (local.file.fwrd != -1){
			DREAD(local.file.fwrd, local);
		}
		//If Offset is greater than end
		if (offset > 0){
			cout << "Invalid offset value." << endl;
			ptr = local.file.data;
			return;
		}
		//If Offset is within one block
		else if (offset <= 0 && offset > -504){
			ptr = local.file.data + offset;
		}
		//If Offset exceeds 1 block
		else{
			//If No other block exists
			if (local.file.back == -1){
				cout << "Invalid offset value." << endl;
				ptr = local.file.data;
				return;
			}
			else{
				while (offset < 0){
					//If Blocks exist move back
					if (local.file.back != -1){
						DREAD(local.file.back, local);
						offset += 504;
					}
					//If Blocks don't exist
					else if ((local.file.back == -1) && (offset < -504)){
						cout << "Invalid offset value." << endl;
						while (local.file.back != -1){
							DREAD(local.file.back, local);
						}
						ptr = local.file.data;
						return;
					}
				}
				ptr = local.file.data + offset;
			}
		}
		break;
	default:
		cout << "Invalid base, please use '-1', '0', or '1'." << endl;
	}

};
void _ls(int _param, int depth){
	int localSec = _param;
	if (localSec == 0){
		cout << "0   d   root\n";
	}
	do{
		DREAD(localSec, local);
		if (local.dir.free == 31)
			return;
		for (int i = 0; i < 31; i++){
			if (local.dir.listing[i].type == 'd'){
				for (int j = 0; j < depth; j++){
					cout << "---";
				}
				cout << local.dir.listing[i].link << "   "
					<< local.dir.listing[i].type << "   "
					<< local.dir.listing[i].name << "\n";
				_ls(local.dir.listing[i].link,depth+1);
				DREAD(localSec, local);
			}
			else if (local.dir.listing[i].type == 'u'){
				for (int j = 0; j < depth; j++){
					cout << "---";
				}
				int size = 0;
				DREAD(local.dir.listing[i].link, local);
				while (local.file.fwrd != -1){
					size++;
					DREAD(local.file.fwrd, local);
				}
				DREAD(localSec, local);
				cout << local.dir.listing[i].link << "   "
					<< local.dir.listing[i].type << "   "
					<< local.dir.listing[i].name << "   "
					<< local.dir.listing[i].size + (504*size) << "\n";
			}
		}
		localSec = local.dir.fwrd;
	} while (localSec != -1);
}
void _ls(int _param){
	if (fState != 0){
		cout << "A File is open. Please close file before attempting to look at directory stucture." << endl;
		return;
	}
	cout << "Free space List: \n";
	for (int i = 0; i < 100; i++){
		cout << FSL[i] << "|";
	}
	cout << endl;
	_ls(_param, 1);
};

int main(){
	FSL[0] = true;
	disk[0].dir.back = -1;
	disk[0].dir.fwrd = -1;
	disk[0].dir.free = 31;
	disk[0].dir.size = 1;
	for (int i = 0; i < 31; i++){
		disk[0].dir.listing[i].type = 'f';
	}

	_create("d Hello");

	_create("d Hello");

	_create("d Hello/World");

	_create("d Hello");

	_create("u Hello/World/Happy");

	_close();

	_create("u Hello/World/Happy");

	_close();

	_delete("Hello");

	_delete("Hello/World/Happ");

	_delete("Hello/World/Happy");

	_delete("Hello/World");

	_create("u Happy");

	_write("1655 'Mullato, A Tragedy of the Deep South, has a meaning and purpose that is faintly expressed in the title itself. Langston Hughes main purpose for writing Mullato is to express the nature of the South as it was even in the 1930s as still a very oppressive and racist South with only a slight improvement from slavery. He contrasts the African Americans of Georgia with Robert who, though Black, was sent to school and traveled in the North as a result. By doing so, he is able to exemplify not only the white resistance to change and oppression towards blacks, but also the fear in the hearts of the African American in the South. It is important to first look at Langstons focus on the fact that the South is far different from the North. “I gave you all a chance and I hope you appreciate it. No other White man in these parts ever did it, as I of.” In the quote Colonel Norwood is speaking to Sallie explaining that what he was doing was unprecedented in the south and he later in the play tells Robert that this was more than was afforded to even some white children. Soon after this scene Higgens drops by and in there conversation he says, “That boy! Hes not gonna be around here long – not the way hes acting.The white folks in townll see to that...I was thinking how weak the doors to the jail is. Theyve brokeem down and lynched four niggers to my memory since its been built...That Bert needs a damn good beating - talking back to a white woman...Thats one yellow buck dont know his place....“Now, Tom, you know that dont go round these parts o Georgia, nor nowhere else in the South.A darkies got to keep in his place down here.” ");

	_close();

	char cmd[9];
	char param1[4];
	char param2[504];
	char param[94];

	while (run){
		cmd[0] = '\0';
		param1[0] = '\0';
		param2[0] = '\0';
		param[0] = '\0';

		cout << "root\\:>> ";

		cin >> cmd;

		if (cin.peek() != '\n')
		{
			cin >> param1;
			strcat(param, param1);
		}

		if (cin.peek() != '\n'){
			cin.getline(param2, 504);
			strcat(param, " ");
			strcat(param, param2);

		}
		for (int i = 0; i < 9; i++){
			cmd[i] = tolower(cmd[i]);
		}
		if (strcmp(cmd, "create") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "CREATE type name\n"
					"type\tCan be either 'U' or 'D'\n"
					"\t'U'\tIndicates a user data file\n"
					"\t'D'\tIndicates a directory file\n"
					"name\tIs a full file name with a maximum length of 9 alphabetical characters\n"
					"\tIf a name is specified and another file or directory already exists,\n\tthe old file or directory( and the contents there in) will be delete and\ta new file or folder will be created in place.\n";
			}
			else{
				_create(param);
			}
		}
		else if (strcmp(cmd, "open") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "OPEN mode name\n"
					"mode\tIs either 'I', 'O', or 'U'\n"
					"\t'I'\tIndicates input mode.\n"
					"\t\tInput mode only 'read' and 'seek' commands are permitted.\n"
					"\t'O'\tIndicates output mode.\n"
					"\t\tOutput mode only 'write' commands are permitted.\n"
					"\t'U'\tIndicates update mode.\n"
					"\t\tUpdate mode allows 'read', 'write' and 'seek' commands.\n"
					"name\tIs the name of the file\n";
			}
			else{
				_open(param);
			}
		}
		else if (strcmp(cmd, "close") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "CLOSE\n"
					"\tCauses the last opened or created file to be closed.\n";
			}
			else{
				_close();
			}
		}
		else if (strcmp(cmd, "delete") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "DELETE filename\n"
					"\t This command will delete the named file.\n";
			}
			else{
				_delete(param);
			}
		}
		else if (strcmp(cmd, "read") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "READ n\n"
					"n\tis the number of bytes to be read from the file that is currently open.\n"
					"\tIf the End of File is before the n bytes specified then\n\tthose bytes will be returned, and EOF message will display.\n";
			}
			else{
				_read(param);
			}
		}
		else if (strcmp(cmd, "write") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "WRITE n 'data'\n"
					"n\tIs the number of bytes that will be written to the currently open file\n\tfrom 'data'.\n"
					"'data'\t Is the data to be written to the file. This field must be enclosed\n\tin single quotes.\n"
					"\tIf n bytes is more than the size of the 'data' parameter, then\n\tblanks will be appened to write n bytes.\n"
					"\tIf the disk is full at any time during the write process, writing will\n\tstop and a Full disk message will be provided.\n";
			}
			else{
				_write(param);
			}
		}
		else if (strcmp(cmd, "seek") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "SEEK base offset\n"
					"base\tCan be '-1', '0', or '1'.\n"
					"\t-1\tIndicates the begining of the file.\n"
					"\t0\tIndicates the current position in the file.\n"
					"\t1\tIndicates the end of the file.\n"
					"offset\tIs a signed integer that indicates the number of\n\tbytes from base the file pointer will be moved to.\n";
			}
			else{
				_seek(param);
			}
		}
		else if (strcmp(cmd, "help") == 0)
		{
			cout << "For more help on any of the commands type in [cmd] \\?\n"
				"CREATE type name\n"
				"OPEN mode name\n"
				"CLOSE\n"
				"DELETE name\n"
				"READ n\n"
				"WRITE n 'data'\n"
				"SEEK base offset\n"
				"LS\n"
				"EXIT\n";
		}
		else if (strcmp(cmd, "ls") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "LS\n"
					"\tWill display the files in the current directories.\n";
			}
			else{
				_ls(atoi(param));
			}
		}
		else if (strcmp(cmd, "clear") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "Clear\n"
					"\tWill print 80 newlines\n";
			}
			else{
				for (int i = 0; i < 80; i++){
					cout << "\n";
				}
			}
		}
		else if (strcmp(cmd, "exit") == 0)
		{
			if (strcmp(param, "/?") == 0)
			{
				cout << "exit \n"
					"Will close the program.\n";
			}
			else{
				run = false;
			}
			return 0;
		}
		else{
			cout << "Command not found. Type 'help' for list of avaliable commands.\n";
		}
	}
}