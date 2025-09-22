#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef  MAZE_IR_H

#define MAZE_IR_H

typedef struct {size_t row; size_t col;} cellId;

typedef enum{
    GRID_OPEN         =  0,
    GRID_WALL         =  1,
    GRID_AGENT_GOAL   =  2,
    GRID_AGENT_START  =  9
} GridCellType;

typedef struct 
{
    size_t rows;
    size_t cols;
    uint8_t* grid;

} MazeInternalRepr;

uint8_t getCell(MazeInternalRepr* m,size_t i, size_t j);
void setCell(MazeInternalRepr* m,size_t i, size_t j, GridCellType t);

cellId getFirstMatchingCell(MazeInternalRepr *m,GridCellType t);
size_t countAllMatchingCells(MazeInternalRepr *m,GridCellType t);

MazeInternalRepr newOpenMaze(size_t rows, size_t cols);

void freeMaze(MazeInternalRepr* m);

#endif

#ifdef MAZE_IR_IMPLEMENTATION

inline void freeMaze(MazeInternalRepr* m){
	free(m->grid);	
};

inline MazeInternalRepr newOpenMaze(size_t rows, size_t cols){
    uint8_t* new_grid = calloc(rows*cols,sizeof(uint8_t));
    return (MazeInternalRepr){.rows=rows,.cols=cols,.grid=new_grid};
}

uint8_t getCell(MazeInternalRepr* m,size_t row, size_t col){
    return m->grid[(row*m->cols)+col];
}

cellId getFirstMatchingCell(MazeInternalRepr *m,GridCellType t){
	for(size_t row=0; row < m->rows; row++)
		for(size_t col=0; col < m->cols; col++){
			if(getCell(m,col,row) == t) return (cellId){(uint8_t)col,(uint8_t)row};
		}
	return (cellId){0,0};
}

size_t countAllMatchingCells(MazeInternalRepr *m,GridCellType t){
	size_t count = 0;
	for(size_t row=0; row < m->rows; row++)
	for(size_t col=0; col < m->cols; col++){
		if(getCell(m,col,row) == t) count++;
	}
	return count;
};

void setCell(MazeInternalRepr* m,size_t i, size_t j, GridCellType t){
    m->grid[(i*m->cols)+j] = t;
}

void debugMazeInternalRepr(MazeInternalRepr* m){
    printf("ROWS: %llu \n",m->rows);
    printf("COLS: %llu \n",m->cols);
    if (m->grid){
		printf("CELLS:\n");
    	for(int i=0; i < m->rows; i++){
			for(int j=0; j < m->cols; j++){
				printf("\t%u",getCell(m,i,j));
			}
			printf("\n");
    	}
	}
	else{
		printf("NO CELLS IN GRID\n");
	}
}


// OBS: THE CELLS ARE 4 BYTES  
int readMazeNumpy(char* file_path,MazeInternalRepr* m){
	
	FILE* f =  fopen(file_path,"rb");
    
	if (f == NULL) {
        perror("[ERRO] Cant Open Numpy File to Read");
        return -1;
    }

	const size_t magic_len = 6;
	const size_t version_len = 2;
	const size_t header_len_bytes = 2; // uint16_t
	
	const char magic_string[] = "\x93NUMPY";
	char temp_string[sizeof(magic_string)];

	uint8_t  major_version,minor_version;
	uint16_t header_len;

	fread(&temp_string,sizeof(char),magic_len,f);

	if (strncmp(magic_string,temp_string,magic_len) != 0){
		perror("[ERRO] File Passed Is Not An Numpy Array");
        return -1;
	}

	fread(&major_version,sizeof(uint8_t),1,f);
	fread(&minor_version,sizeof(uint8_t),1,f);
	fread(&header_len,sizeof(uint16_t),1,f);

	char* header_data = calloc(header_len+1,sizeof(char));
	fread(header_data,sizeof(char),header_len,f);
	header_data[header_len] = '\0';

	// printf("%.*s\n",header_len,header_data);
	char* last_two_dots = NULL;
	
	for (size_t i = header_len; i-- > 0;) {
		if (header_data[i] == ':') {
			last_two_dots = &header_data[i];
			break; 
		}
	}
	sscanf(last_two_dots,": (%u, %u), }",&m->rows,&m->cols);

	// READING DATA ARRAY
	size_t total_header = magic_len + version_len + header_len_bytes + header_len;

	if (m->grid) {
		uint8_t* tmp = realloc(m->grid, m->rows * m->cols * sizeof(uint8_t));
		if (!tmp) { 
			perror("[ERRO] realloc failed"); 
			free(m->grid);
			return -1;
		}
		m->grid = tmp;
	} else {
		m->grid = calloc(m->rows * m->cols, sizeof(uint8_t));
		if (!m->grid) {
			perror("[ERRO] calloc failed");
			return -1;
		}
	}

	// alinhar para múltiplo de 16 (padding)
	size_t padding = (16 - (total_header % 16)) % 16;

	// offset dos dados
	size_t data_offset = total_header + padding;
	
	fseek(f, data_offset, SEEK_SET);

	int32_t temp = 0;
	for(size_t i=0; i < m->rows*m->cols; i++){
		if(fread(&temp, sizeof(int32_t), 1, f) != 1){
			perror("Failed reading maze data");
			return -1;
		}
		m->grid[i] = (uint8_t)temp;
	}

	free(header_data);
	fclose(f);
	return 0;
};


/*
    MAZE RAW FILE FORMAT SPECS
    EXTENSION:
        .maze

    HEADER:
        | NUMBER OF ROWS : 64 bit | NUMBER OF COLS : 64 bit |
    PAYLOAD:
        | cell0: 8 bit | cell1: 8 bit | cell2: 8 bit | ... | celln: 8bit

    THE NUMBER OF CELLS IS (ROWS * COLS) EACH CELL IS AN 1 BYTE NUMBER

*/

int writeMazeRaw(char* file_path,MazeInternalRepr *m){

    FILE* f =  fopen(file_path,"wb");

    if (f == NULL) {
        perror("[ERRO] Cant Open Raw Maze File To Write");
        return -1;
    }

    fwrite(&m->rows,sizeof(size_t),1,f);
    fwrite(&m->cols,sizeof(size_t),1,f);
    fwrite(m->grid,sizeof(uint8_t),m->rows*m->cols,f);
    
    fclose(f);

	return 0;
};

int readMazeRaw(char* file_path,MazeInternalRepr *m){

    FILE* f =  fopen(file_path,"rb");

    if (f == NULL) {
        perror("[ERRO] Cant Open Raw Maze File To Read");
        return -1;
    }

    fread(&m->rows,sizeof(size_t),1,f);
    fread(&m->cols,sizeof(size_t),1,f);

    if(m->grid != NULL) free(m->grid);
    
    size_t maze_size= m->rows * m->cols;

    m->grid = calloc(maze_size, sizeof(uint8_t));

    fread(m->grid,sizeof(uint8_t),maze_size,f);  

    fclose(f);

	return 0;
};



#endif