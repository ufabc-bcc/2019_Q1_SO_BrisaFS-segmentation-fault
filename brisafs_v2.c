/*
 * Emilio Francesquini <e.francesquini@ufabc.edu.br>
 * 2019-02-03
 *
 * Este código foi criado como parte do enunciado do projeto de
 * programação da disciplina de Sistemas Operacionais na Universidade
 * Federal do ABC. Você pode reutilizar este código livremente
 * (inclusive para fins comerciais) desde que sejam mantidos, além
 * deste aviso, os créditos aos autores e instituições.
 *
 * Licença: CC-BY-SA 4.0
 *
 * O código abaixo implementa (parcialmente) as bases sobre as quais
 * você deve construir o seu projeto. Ele provê um sistema de arquivos
 * em memória baseado em FUSE. Parte do conjunto minimal de funções
 * para o correto funcionamento do sistema de arquivos está
 * implementada, contudo nem todos os seus aspectos foram tratados
 * como, por exemplo, datas, persistência e exclusão de arquivos.
 *
 * Em seu projeto você precisará tratar exceções, persistir os dados
 * em um único arquivo e aumentar os limites do sistema de arquivos
 * que, para o código abaixo, são excessivamente baixos (ex. número
 * total de arquivos).
 *
 */


#define FUSE_USE_VERSION 31

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

/* Número máximo de arquivos */
#define N_FILES 1024
/* Tamanho máximo de um arquivo */
#define MAX_FILE_SIZE 64000000
/* Tamanho do bloco do dispositivo */
#define TAM_BLOCO 4096

/* Quantidade de arquivos por bloco */
#define MAX_FILES (TAM_BLOCO / sizeof(inode))

/* Quantidade mínima de blocos para armazenar 4 arquivos de tamanho MAX_FILE_SIZE*/
/* Forçar o arredonamento para cima: q = x/y = 1+((x-1)/y) */
#define MIN_DATABLOCKS (4*(1+((MAX_FILE_SIZE-1) / TAM_BLOCO)))

/* Quantidade mínima de blocos para comportar todos os inodes */
/* Forçar o arredonamento para cima: q = x/y = 1+((x-1)/y) */
#define N_SUPERBLOCKS (1+(((MIN_DATABLOCKS * sizeof(inode))-1) / TAM_BLOCO))

/* Total de blocos necessários para o sistema de arquivos */
#define MAX_BLOCOS (N_SUPERBLOCKS + MIN_DATABLOCKS)

/* Direitos -rw-r--r-- */
#define DIREITOS_PADRAO 0644

/* Função para calcular o offset de blocos */
#define DISCO_OFFSET(B) (B * TAM_BLOCO)

/* Definição de byte, que nada mais é que um char */
typedef char byte;

/* Um inode guarda todas as informações relativas a um arquivo como
   por exemplo nome, direitos, tamanho, bloco inicial, ... */
typedef struct {
    uint16_t id; // 2 bytes
    char nome[224]; // 224 bytes
    mode_t type; // 4 bytes
    uint32_t timestamp[2]; //0: Modificacao, 1: Acesso
    uint16_t direitos;
    uint32_t tamanho;
    uint16_t bloco;
		uint16_t proxbloco;
    uid_t userown; // 4 bytes
    gid_t groupown; // 4 bytes
} inode;

/* Disco - A variável abaixo representa um disco que pode ser acessado
   por blocos de tamanho TAM_BLOCO com um total de MAX_BLOCOS. */
byte *disco;

/* Ponteiro para um inode */
inode *superbloco;

/* Ponteiro de diretório */
byte *dir;

/* Quantidade de blocos disponíveis em disco (inicialmente o disco está vazio)*/
int free_space = N_SUPERBLOCKS;

/* Cabeçalhos de Funções */
int armazena_data(int typeop, int inode);
int quebra_nome (const char *path, char **name, char **parent);
int compara_nome (const char *path, const char *nome);
uint16_t dir_tree (const char *path);

/* Função que salva o disco (RAM) no arquivo hdd1 (persistente) */
void salva_disco(){
	FILE *file  = fopen ("hdd1", "wb");
  fwrite (disco, TAM_BLOCO, MAX_BLOCOS, file);
  printf ("Salvando arquivo HDD\n");
  fclose (file);
}

/* Função que verifica se o arquivo hdd1 já existe.
Em caso positivo, carrega todo o conteúdo do arquivo hdd1 para a memória principal */
int carrega_disco() {
  size_t result;
  size_t lSize;
  if (access("hdd1", F_OK) == 0){
  	FILE *file = fopen ("hdd1", "rb");
    lSize = ftell (file);
    result = fread (disco,TAM_BLOCO,MAX_BLOCOS,file);
    fclose (file);
    printf ("Carregou...\n");
    printf ("lSize = %lu\n", lSize);
    printf ("result = %lu\n", result);
    
    // Esse loop atualiza a variável free_space
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
    	if(superbloco[i].bloco != 0)
    		free_space--;
    }
    return 1;
  } else
    return 0;
}

/* Preenche os campos do superbloco do primeiro bloc vazio que encontrar */
int preenche_bloco (const char *nome, uint16_t direitos, uint16_t tamanho, 
											const byte *conteudo, mode_t type) {
  
  // Quantidade de blocos que o arquivo ocupa
  int num_blocos = (1+((tamanho-1) / TAM_BLOCO));
  // Variável para fazer o link entre dois blocos de um mesmo arquivo
  int bloco_anterior = 0;
  
	printf("Blocos necessários para o arquivo: %d\n", num_blocos);
	
	if (tamanho > MAX_FILE_SIZE) {
		printf("Tamanho máximo de arquivo excedido!\n");
		return 1; //EFBIG
		
	} else if (num_blocos > free_space) {
		printf("Não há espaço suficiente em disco para este arquivo!\n");
		return 2; //ENOSPC
	}
  
  for (int k = 0; k < num_blocos; k++) {
		printf("k = %d\n", k);
		printf("N_SUPERBLOCKS = %lu\n", N_SUPERBLOCKS);
		for (int isuperbloco = 0; isuperbloco < N_SUPERBLOCKS; isuperbloco++) {
  		if (superbloco[isuperbloco].bloco == 0) { //ninguem usando
    		uint16_t bloco = N_SUPERBLOCKS + isuperbloco + 1;
    		printf("Inode vazio: %d - Bloco vazio %d\n", isuperbloco, bloco);
    		if (k == 0) { //primeiro bloco
    			printf("Entrei no k = 0\n");
    			char *mnome = NULL;
    			char *pai = NULL;

    			quebra_nome(nome, &mnome, &pai);

    			superbloco[isuperbloco].id = isuperbloco;
    			strcpy(superbloco[isuperbloco].nome, mnome);
    			superbloco[isuperbloco].direitos = direitos;
    			superbloco[isuperbloco].tamanho = tamanho;
    			superbloco[isuperbloco].bloco = bloco;
    			superbloco[isuperbloco].type = type;
    			superbloco[isuperbloco].proxbloco = 0;
    			armazena_data (0, isuperbloco);
    			
    			bloco_anterior = isuperbloco;

    			/* Para qualquer tipo de arquivo, exceto o diretório root, procura o 
    			inode do diretório pai e se inclui dentro do bloco do diretório pai*/
    			if (strcmp(mnome,"/")!=0) {
    				dir = disco + DISCO_OFFSET(superbloco[dir_tree(pai)].bloco);
						uint16_t *d = (uint16_t*) dir;
						d[0]++;
						d[d[0]] = isuperbloco;
    			}
    			
    			free(mnome);
    			free(pai);
    			
    			// Se for um diretório, inicializa informando que está vazio
    			if (type == S_IFDIR) {
    				dir = disco + DISCO_OFFSET(bloco);
    				uint16_t *d = (uint16_t*) dir;
    				d[0] = 0;
    				return 0;
    			} else { //Se for arquivo, grava o conteúdo
    				if (conteudo != NULL) {
    					printf("Conteúdo: %s\n", conteudo);
    					printf("Tamanho do Conteúdo: %lu\n", sizeof(conteudo));
    					if (sizeof(conteudo) > TAM_BLOCO) {
      	 				memcpy(disco + DISCO_OFFSET(bloco), conteudo, TAM_BLOCO);
      	 				free_space--;
								break;
							} else {
								memcpy(disco + DISCO_OFFSET(bloco), conteudo, tamanho);
								free_space--;
								return 0;
							}
        		}
    				else {
    					printf("Criei um arquivo vazio!\n");
        			memset(disco + DISCO_OFFSET(bloco), 0, tamanho);
        			free_space--;
        			return 0;
        		}
    			}
    		} else { /* Se o conteudo precisa de mais que um bloco, executa esse 
    								else para os demais blocos */
    			superbloco[isuperbloco].id = isuperbloco;
    			superbloco[isuperbloco].bloco = bloco;
					superbloco[isuperbloco].proxbloco = 0;
					
					superbloco[bloco_anterior].proxbloco = isuperbloco;
					bloco_anterior = isuperbloco;
					
					printf("Conteúdo: %s\n", conteudo);
    			printf("Tamanho do Conteúdo: %lu\n", sizeof(conteudo));
					if (sizeof(conteudo) > TAM_BLOCO) {
      	  	memcpy(disco + DISCO_OFFSET(bloco), conteudo + DISCO_OFFSET(k), TAM_BLOCO);
      	  	free_space--;
						break;
					} else {
						memcpy(disco + DISCO_OFFSET(bloco), conteudo + DISCO_OFFSET(k), tamanho);
						free_space--;
						return 0;
					}
    		}
			}
		}
	}
	return 2; //ENOSPC
}

/* Inicializa o sistema de arquivos */
void init_brisafs() {
  disco = calloc (MAX_BLOCOS, TAM_BLOCO);
  superbloco = (inode*) disco; //posição 0
  dir = (byte*) disco; //posição 0

  if(carrega_disco() == 0){
    //Cria o diretório raiz
    preenche_bloco ("/", DIREITOS_PADRAO, 64, NULL, S_IFDIR);
    //Cria um arquivo com as configurações do sistema de arquivos
    char *nome = "/BrisaFS.txt";
    
    char str1[50];
  	char str2[50];
  	char str3[50];
  	char str4[50];
  	char str5[50];
  	char str6[50];
  	char str7[50];
  	char str8[50];
  	char str9[50];
  	char *str = malloc(320);
    
    sprintf(str1, "Configurações do BrisaFS:\n");
		sprintf(str2, "\t Tamanho do bloco = %d bytes\n", TAM_BLOCO);
    sprintf(str3, "\t Tamanho máximo de arquivo = %d bytes\n", MAX_FILE_SIZE);
    sprintf(str4, "\t Tamanho do inode: %lu bytes\n", sizeof(inode));
    sprintf(str5, "\t Quantidade de inodes: %u\n", MIN_DATABLOCKS);
    sprintf(str6, "\t Número máximo de inodes por superboco: %lu\n", MAX_FILES);
    sprintf(str7, "\t Número de superbocos: %lu\n", N_SUPERBLOCKS);
    sprintf(str8, "\t Quantidade de blocos no disco: %lu\n", MAX_BLOCOS);
    sprintf(str9, "\t Tamanho do Disco: %lu bytes\n", TAM_BLOCO * MAX_BLOCOS);
    
    strcpy(str, str1);
		strcat(str, str2);
		strcat(str, str3);
		strcat(str, str4);
		strcat(str, str5);
		strcat(str, str6);
		strcat(str, str7);
		strcat(str, str8);
		strcat(str, str9);
    
    // Cria o primeiro arquivo no inode 1
    preenche_bloco(nome, DIREITOS_PADRAO, strlen(str), (byte*) str, S_IFREG);
    free(str);
  }
}

/* Recebe o path de um arquivo e retorna o nome do arquivo e o path restante*/
int quebra_nome (const char *path, char **name, char **parent) {
  char* c1 = strdup(path);
	char* c2 = strdup(path);
	
	*parent = (char*)malloc(sizeof(char)*strlen(c1));
  *name = (char*)malloc(sizeof(char)*strlen(c1));
	if (parent == NULL || name == NULL)
		return 1;
	
	char* n = basename(c1);
	char* p = dirname(c2);
	if (p == NULL || n == NULL)
		return 2;

	strcpy(*name, n);
	strcpy(*parent, p);

  return 0;
}

/* Devolve 1 caso o nome do arquivo seja o mesmo do indicado pelo path e 0 cc */
int compara_nome (const char *path, const char *nome) {
	char *mn = NULL;
  char *mp = NULL;

  quebra_nome (path, &mn, &mp);

	if (strcmp(nome, mn) == 0) {
		free(mn);
		free(mp);
		return 1;
	}
		
	free(mn);
	free(mp);
  return 0;
}

// Recebe um path e retorna o id do inode indicado pelo path
uint16_t dir_tree (const char *path) {
	int count = 0; // Contador de nivel de hierarquia
	uint16_t id = MIN_DATABLOCKS + 1; // id fora do range que será retornado se não encontrar
	
	// Variáveis necessárias para utilizar a libgen
	char* n0 = strdup(path);
	char* p0 = strdup(path);
	
	// Loop para contar a quantidade de camadas até o arquivo indicado por path
	while (strcmp(p0,"/") != 0) {
		char* p1 = strdup(p0);
		char* p2 = strdup(p0);
		n0 = basename(p1);
		p0 = dirname(p2);
		count ++;
	}
	
	if (strcmp(n0,"/") == 0)
		return 0;
	
	// Loop para percorrer o path
	for(int k = count; k > 0; k--) {
		n0 = strdup(path);
		p0 = strdup(path);
	
		for(int w = 0; w < k; w++) {
			char* p1 = strdup(p0);
			char* p2 = strdup(p0);
			n0 = basename(p1);
			p0 = dirname(p2);
		}
		
		// Condicional para o caso do diretório raiz
		if (k == count) {
			for (int i = 0; i < N_SUPERBLOCKS; i++) { // Percorre os inodes
  			if (superbloco[i].bloco != 0 && strcmp(p0, superbloco[i].nome) == 0) { // Achou o pai
    			dir = disco + DISCO_OFFSET(superbloco[i].bloco); // Aponta dir para o bloco do pai
    			uint16_t *d = (uint16_t*) dir; // Faz um casting para uint16_t
    			
    			// Varre todos os arquivos dentro do diretório pai
    			for(int j = 1; j <= d[0]; j++) {
    				if (strcmp(n0, superbloco[d[j]].nome) == 0) { // Achou!
							id = d[j]; // Grava o id do inode
							break; // Vai para o próximo step do path
						}
					}
					break;
				}
			}
		} else { //Se não for o diretório raiz
			dir = disco + DISCO_OFFSET(superbloco[id].bloco); // Aponta dir para o bloco do pai
    	uint16_t *d = (uint16_t*) dir; // Faz um casting para uint16_t
    	
    	// Varre todos os arquivos dentro do diretório pai
    	for(int j = 1; j <= d[0]; j++) {
    		if (strcmp(n0, superbloco[d[j]].nome) == 0) { // Achou!
					id = d[j]; // Grava o id do inode
				}
			}
		}
	}
	// Ao final, o inode que estiver na variável id, é o inode indicado pelo path
  return id; 
}

// Armazena a data de criação ou modificação do inode
int armazena_data (int typeop, int inode){
  struct timeval time;
  gettimeofday (&time, NULL);

  if (typeop == 0) { // Modificacao
    superbloco[inode].timestamp[0] = time.tv_sec;
    superbloco[inode].timestamp[1] = time.tv_sec;
    return 0;
  } else if (typeop == 1) { // Acesso
    superbloco[inode].timestamp[1] = time.tv_sec;
    return 0;
  }
  return 1; //Caso operacao invalide
}


/* A função getattr_brisafs devolve os metadados de um arquivo cujo
   caminho é dado por path. Devolve 0 em caso de sucesso ou um código
   de erro. Os atributos são devolvidos pelo parâmetro stbuf */
static int getattr_brisafs(const char *path, struct stat *stbuf) {
	memset(stbuf, 0, sizeof(struct stat));

  //Diretório raiz
  if (strcmp(path, "/") == 0) {
  	stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  char *subdir = NULL;
  char *filename = NULL;
  quebra_nome(path, &filename, &subdir);

  for(int i = 0; i < N_SUPERBLOCKS; i++){
    if(superbloco[i].bloco != 0 && S_ISDIR(superbloco[i].type) && 
       compara_nome(subdir, superbloco[i].nome)) {

			// Se o id do inode for igual ao do inode analisado, prossegue
			if (dir_tree(subdir) == i) {
        dir = disco + DISCO_OFFSET(superbloco[i].bloco); // Aponta dir para o bloco do pai
    		uint16_t *d = (uint16_t*) dir; // Faz um casting para uint16_t
    		
    		//Procura se existe um arquivo dentro do pai correspondente ao arquivo buscado
    		for(int j = 1; j <= d[0]; j++) {
    			//Se o arquivo existe no diretorio, preenche as informacoes ao buffer
    			if(strcmp(filename,superbloco[d[j]].nome) == 0) {
            stbuf->st_mode = superbloco[d[j]].type | superbloco[d[j]].direitos;
            stbuf->st_nlink = 1;
            stbuf->st_size = superbloco[d[j]].tamanho;
            stbuf->st_mtime = superbloco[d[j]].timestamp[0];
            stbuf->st_atime = superbloco[d[j]].timestamp[1];
            stbuf->st_uid = superbloco[d[j]].userown;
            stbuf->st_gid = superbloco[d[j]].groupown;
            return 0;
          }
    		}
        return -ENOENT;//Caso nao encontre o arquivo
      }
    }
	}
  return -ENOENT; //Caso nao encontre o subdiretorio/pai
}

/* Devolve ao FUSE a estrutura completa do diretório indicado pelo
   parâmetro path. Devolve 0 em caso de sucesso ou um código de
   erro. Atenção ao uso abaixo dos demais parâmetros. */
static int readdir_brisafs(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi) {
	(void) offset;
  (void) fi;

  filler(buf, ".", NULL, 0);
  filler(buf, "..", NULL, 0);
	
	uint16_t id = dir_tree(path);
	if (id > MIN_DATABLOCKS)
		return -ENOENT;
	
  dir = disco + DISCO_OFFSET(superbloco[id].bloco);
  uint16_t *d = (uint16_t*) dir;
  for(int j = 1; j <= d[0]; j++) {
  	filler(buf, superbloco[d[j]].nome, NULL, 0);
  }
  return 0;
}

/* Abre um arquivo. Caso deseje controlar os arquvos abertos é preciso
   implementar esta função */
static int open_brisafs(const char *path, struct fuse_file_info *fi) {
    return 0;
}

/* Função chamada quando o FUSE deseja ler dados de um arquivo
   indicado pelo parâmetro path. Se você implementou a função
   open_brisafs, o uso do parâmetro fi é necessário. A função lê size
   bytes, a partir do offset do arquivo path no buffer buf. */
static int read_brisafs(const char *path, char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi) {
		
	uint16_t id = dir_tree(path);
	if (id > MIN_DATABLOCKS)
		return -ENOENT; // Arquivo não encontrado
  
  // Tamanho do arquivo a ser lido
  size_t len = superbloco[id].tamanho;
  // Quantidade de blocos que o read vai ler
  int num_blocos = (1+((size-1) / TAM_BLOCO));
  // Bloco sendo lido
  uint16_t supb = id;
  // Quantidade lida
  uint32_t read_size;
  // Faltando ser lido
  uint32_t remaining_size;
   
  printf("Caminho: %s\n", path);
  //printf("Buf: %s\n", buf);
  printf("Tamanho: %ld\n", size);
  printf("Offset: %ld\n", offset);
  printf("len: %ld\n", len);
  printf("Blocos que o arquivo ocupa: %d\n", num_blocos);
  armazena_data(1, id);
  
  if (offset >= len) {//tentou ler além do fim do arquivo
    printf("Tentou ler o fim do arquivo\n");
    return 0;
  }
  
  if (offset + size > TAM_BLOCO) {
  	if (offset > TAM_BLOCO) {
  		read_size = 0;
  		remaining_size = size;
  		uint32_t read_offset = 0;
  		
  		// Loop para chegar até o offset de leitura
  		while (read_offset < (offset - TAM_BLOCO)) {
  			if (superbloco[supb].proxbloco != 0) {
  				supb = superbloco[supb].proxbloco;
  				read_offset = read_offset + TAM_BLOCO;
  			} else {
  				return 0;
  			}
  		}
  	} else {
  		printf("offset + size maior que TAM_BLOCO\n");
  		read_size = TAM_BLOCO - offset;
  		remaining_size = size - read_size;
  		printf("1. Lendo inode %u referente ao bloco %u\n", superbloco[id].id, superbloco[id].bloco);
    	memcpy(buf, disco + DISCO_OFFSET(superbloco[id].bloco) + offset, read_size);
    	supb = superbloco[supb].proxbloco;
  	}
  	/*
  	printf("offset + size maior que len\n");
    memcpy(buf, disco + DISCO_OFFSET(superbloco[id].bloco), len - offset);
    printf("Buf: %s\n", buf);
    return len - offset;*/
  } else {
  	printf("2. Lendo inode %u referente ao bloco %u\n", superbloco[id].id, superbloco[id].bloco);
  	memcpy(buf, disco + DISCO_OFFSET(superbloco[id].bloco) + offset, size);
  	return size;
  }
  
  // Laço para leitura dos blocos extras				
	for (int k = 0; k < num_blocos; k++) {
		printf("EB: %d/%d\n", k, num_blocos);
		printf("Read Size: %d\n", read_size);
  	printf("Remaining Size: %u\n", remaining_size);
		
		if (remaining_size > TAM_BLOCO) {
			printf("EB1. Lendo inode %u referente ao bloco %u\n", superbloco[supb].id, superbloco[supb].bloco);
			memcpy(buf+read_size, disco + DISCO_OFFSET(superbloco[supb].bloco), TAM_BLOCO);
    	supb = superbloco[supb].proxbloco;
			read_size = read_size + TAM_BLOCO;
  		remaining_size = size - read_size;
		} else {
			printf("EB2. Lendo inode %u referente ao bloco %u\n", superbloco[supb].id, superbloco[supb].bloco);
			memcpy(buf+read_size, disco + DISCO_OFFSET(superbloco[supb].bloco), remaining_size);
  		return size;
		}
	}
	return 0;
}

/* Função chamada quando o FUSE deseja escrever dados em um arquivo
   indicado pelo parâmetro path. Se você implementou a função
   open_brisafs, o uso do parâmetro fi é necessário. A função escreve
   size bytes, a partir do offset do arquivo path no buffer buf. */
   //Em caso de Segmatation fault: fusermount -u <dir>
static int write_brisafs(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {
	
	printf("Path: %s\n", path);
  //printf("Buf: %s\n", buf);
  //printf("Tamanho do Buf: %ld\n", strlen(buf));
  printf("Size: %ld\n", size);
  printf("Offset: %ld\n", offset);
	
  uint16_t id = dir_tree(path);
  if (id > MIN_DATABLOCKS)
		return -ENOENT; // Arquivo não encontrado
	
	printf("Tamanho atual: %d\n", superbloco[id].tamanho);
		
  // Quantidade de blocos que o arquivo ocupa atualmente
  int ini_blocos = (1+((superbloco[id].tamanho-1) / TAM_BLOCO));
  // Quantidade de blocos que o arquivo precisará
  int fim_blocos = (1+((superbloco[id].tamanho+size-1) / TAM_BLOCO));
  // Quantidade de blocos a mais
  int ext_blocos = fim_blocos - ini_blocos;
  // Variáveis auxiliares
  uint16_t supb = id;
  uint32_t wrt_size;
  uint32_t remaining_size;
  				
	printf("Blocos que o arquivo ocupa atualmente: %d\n", ini_blocos);
	printf("Blocos que o arquivo precisará: %d\n", fim_blocos);
	printf("Blocos extras que o arquivo precisará: %d\n", ext_blocos);
	//printf("Write Size: %d\n", wrt_size);
	//printf("Remaining Size: %ld\n", remaining_size);
	
	if (superbloco[id].tamanho + size > MAX_FILE_SIZE) {
		printf("Tamanho máximo de arquivo excedido!\n");
		return EFBIG;
	} else if (ext_blocos > free_space) {
		printf("Não há espaço suficiente em disco para este arquivo!\n");
		return ENOSPC;
	}
  
  // Pega o último bloco do arquivo    	
  while (superbloco[supb].proxbloco != 0) {
  	supb = superbloco[supb].proxbloco;
  }
	
	/* Se tamanho do buffer a ser escrito não couber no último bloco já existente 
	do arquivo, preenche apenas o que couber */
  if (offset + size > TAM_BLOCO) {
  	if (offset < TAM_BLOCO) {
  		wrt_size = TAM_BLOCO - offset;
  		remaining_size = size - wrt_size;
  		printf("1. To escrevendo %d bytes no bloco %u referente ao inode %u\n\n", 
        		wrt_size, superbloco[supb].bloco, superbloco[supb].id);
    	memcpy(disco + DISCO_OFFSET(superbloco[supb].bloco) + offset, buf, wrt_size);
    	superbloco[id].tamanho = offset + wrt_size;
    	armazena_data(0, id);
  	} else {
  		printf("Veio aqui...\n");
  		wrt_size = 0;
  		remaining_size = size;
  	}
  	// Não tem return, pois ainda falta salvar o resto dos dados
	} else { // Se o buffer couber no último bloco já existente do arquivo
		printf("2. To escrevendo %ld bytes no bloco %u referente ao inode %u\n\n", 
        		size, superbloco[supb].bloco, superbloco[supb].id);
		memcpy(disco + DISCO_OFFSET(superbloco[supb].bloco) + offset, buf, size);
		superbloco[id].tamanho = offset + size;
    armazena_data(0, id);
    printf("Terminei!\n");
		return size;
	}
	
	// Laço para a criação dos blocos extras para acomodar todo o buffer				
	for (int k = 0; k < ext_blocos; k++) {
		printf("EB - k = %d\n", k);
		for (int isb = 0; isb < N_SUPERBLOCKS; isb++) {
  		if (superbloco[isb].bloco == 0) { //ninguem usando
    		uint16_t bloco = N_SUPERBLOCKS + isb + 1;
    		printf("EB - Inode vazio: %d - Bloco vazio %d\n", isb, bloco);
    		
    		// Preenche apenas o essencial, pois o primeiro inode já tem as infos do arquivo		
    		superbloco[isb].id = isb;
    		superbloco[isb].bloco = bloco;
    		
    		// O último bloco do arquivo agora passa a ser esse recém criado
				superbloco[isb].proxbloco = 0; // 0 indica ultimo bloco
				superbloco[supb].proxbloco = isb; // O penultimo bloco aponta para o último
				// Salva o último bloco para futura referência de um eventual novo último bloco
				supb = isb; 
					
				//printf("Conteúdo: %s\n", conteudo);
    		//printf("Tamanho do Conteúdo: %lu\n", sizeof(conteudo));
    		
    		// Se todo o buffer restante não couber em um bloco 
				if (remaining_size > TAM_BLOCO) {
					printf("EB1. To escrevendo %d bytes no bloco %u referente ao inode %u\n\n", 
        		TAM_BLOCO, superbloco[supb].bloco, superbloco[supb].id);
      		memcpy(disco + DISCO_OFFSET(bloco), buf + wrt_size, TAM_BLOCO);
      	  wrt_size = wrt_size + TAM_BLOCO;
      	  remaining_size = size - wrt_size;
      	  superbloco[id].tamanho = offset + wrt_size;
      	 	armazena_data(0, id);
      	  free_space--;
					break;
				} else { // Se todo o buffer restante couber neste bloco
					printf("EB2. To escrevendo %d bytes no bloco %u referente ao inode %u\n\n", 
        		remaining_size, superbloco[supb].bloco, superbloco[supb].id);
					memcpy(disco + DISCO_OFFSET(bloco), buf + wrt_size, remaining_size);
					wrt_size = wrt_size + remaining_size;
					superbloco[id].tamanho = offset + wrt_size;
      	 	armazena_data(0, id);
					free_space--;
					return size;
				}
			}
		}
	}

  //Se chegou aqui não achou. Entao cria
  //Acha o primeiro bloco vazio
  if (preenche_bloco (path, DIREITOS_PADRAO, size, buf, S_IFREG) == 0) {
  	printf("EU ENTREI AQUI\n");
  	return size;
  }
  return -EIO;
}

// Remove um arquivo
static int unlink_brisafs(const char *path) {
	
	char *subdir = NULL;
  char *filename = NULL;
  quebra_nome(path, &filename, &subdir);
  
  uint16_t id = dir_tree(subdir);
  if (id > MIN_DATABLOCKS)
		return -ENOENT; // Arquivo não encontrado
	
  dir = disco + DISCO_OFFSET(superbloco[id].bloco);
  uint16_t *d = (uint16_t*) dir;
  // Varre todos os arquivos dentro do pai para encontrar o arquivo
  for(int j = 1; j <= d[0]; j++) {
  	if (compara_nome(path, superbloco[d[j]].nome)) { // achou!
    	// Informa que o bloco está disponível para gravação
      uint16_t sb = d[j];
      while (sb != 0) {
      	superbloco[sb].bloco = 0;
      	superbloco[sb].tamanho = 0;
      	sb = superbloco[sb].proxbloco;
      } 
      
      // Remove o arquivo do diretório pai
      d[0]--;
      for(int w = j; w <= d[0]; w++) {
      	d[w] = d[w+1];
      }
      return 0;
		}
  }
  return -ENOENT; // Arquivo não encontrado
}

// Remove um diretório, assim como todos os arquivos dentro dele
static int rmdir_brisafs (const char *path) {
	
	char *subdir = NULL;
  char *filename = NULL;
  quebra_nome(path, &filename, &subdir);
  
  uint16_t id = dir_tree(subdir);
  if (id > MIN_DATABLOCKS)
		return -ENOENT; // Arquivo não encontrado
	
	// Leva o ponteiro d0 até o pai do diretório que será apagado
  dir = disco + DISCO_OFFSET(superbloco[id].bloco);
  uint16_t *d0 = (uint16_t*) dir;
  // Varre todos os arquivos dentro do pai para encontrar o diretório
  for(int j = 1; j <= d0[0]; j++) {
  	if (compara_nome(path, superbloco[d0[j]].nome)) { // achou!
    	
    	// Leva o ponterio d1 para o diretório que será apagado
    	dir = disco + DISCO_OFFSET(superbloco[d0[j]].bloco);
  		uint16_t *d1 = (uint16_t*) dir;
    	
    	//Varre o diretório que será apagado, apagando todos os arquivos internos
    	for(int i = 1; i <= d1[0]; i++) {
  			if (superbloco[d1[i]].bloco == 0) { //achou um arquivo dentro do diretorio*/
    			// Informa que o bloco está disponível para gravação
      		uint16_t sb = d1[i];
      		while (sb != 0) {
      			superbloco[sb].bloco = 0;
      			superbloco[sb].tamanho = 0;
      			sb = superbloco[sb].proxbloco;
      		} 
    		}
    	}
    	
    	// Apaga o diretório, informando que está disponível para gravação
      superbloco[d0[j]].bloco = 0;
      superbloco[d0[j]].tamanho = 0;
      		
      // Remove o diretório do diretório pai
      d0[0]--;
      for(int w = j; w <= d0[0]; w++) {
      	d0[w] = d0[w+1];
      }
      return 0;
		}
  }
  return -ENOENT; // Arquivo não encontrado
}

/* Altera o tamanho do arquivo apontado por path para tamanho size
   bytes */
static int truncate_brisafs(const char *path, off_t size) {
  if (size > MAX_FILE_SIZE) {
  	return EFBIG;
	}
	
	uint16_t findex = MIN_DATABLOCKS + 1;

  findex = dir_tree(path);	
	
  //procura o arquivo
  if (findex <= MIN_DATABLOCKS) {// arquivo existente
  	superbloco[findex].tamanho = size;
    return 0;
  } else {// Arquivo novo
    //Acha o primeiro bloco vazio
  	preenche_bloco (path, DIREITOS_PADRAO, size, NULL, S_IFREG);
  }
  return 0;
}

/* Cria um arquivo comum ou arquivo especial (links, pipes, ...) no caminho
   path com o modo mode*/
static int mknod_brisafs(const char *path, mode_t mode, dev_t rdev) {
	if (S_ISREG(mode)) { //So aceito criar arquivos normais
		//Cuidado! Não seta os direitos corretamente! Veja "man 2
    //mknod" para instruções de como pegar os direitos e demais
    //informações sobre os arquivos
    //Acha o primeiro bloco vazio
    if(preenche_bloco (path, DIREITOS_PADRAO, 0, NULL, S_IFREG) == 0) {
    	return 0;
		}
    return ENOSPC;
  }
  return EINVAL;
}


/* Sincroniza escritas pendentes (ainda em um buffer) em disco. Só
   retorna quando todas as escritas pendentes tiverem sido
   persistidas */
static int fsync_brisafs(const char *path, int isdatasync,
                         struct fuse_file_info *fi) {
	return 0;
}

/* Ajusta a data de acesso e modificação do arquivo com resolução de nanosegundos */
static int utimens_brisafs(const char *path, const struct timespec ts[2]) {
    // Cuidado! O sistema BrisaFS não aceita horários. O seu deverá aceitar!
	return 0;
}

static int chown_brisafs(const char *path, uid_t userowner, gid_t groupowner){
  printf("O GRUPO É: %d\n", groupowner);
  printf("O USUARIO É: %d\n", userowner);
  
  uint16_t id = dir_tree(path);
  if (id > MIN_DATABLOCKS)
		return -ENOENT; // Arquivo não encontrado
		
  if(userowner != -1)
  	superbloco[id].userown = userowner;

  if(groupowner != -1)
  	superbloco[id].groupown = groupowner;

  return 0;
}

static int chmod_brisafs(const char *path, mode_t mode) {
	
	uint16_t id = dir_tree(path);
  if (id > MIN_DATABLOCKS)
		return -ENOENT; // Arquivo não encontrado
	
  superbloco[id].direitos = mode;
  
  return 0;
}

/* Cria e abre o arquivo apontado por path. Se o arquivo não existir
   cria e depois abre*/
static int create_brisafs(const char *path, mode_t mode,
                          struct fuse_file_info *fi) {
	//Cuidado! Está ignorando todos os parâmetros. O seu deverá
	//cuidar disso Veja "man 2 mknod" para instruções de como pegar os
	//direitos e demais informações sobre os arquivos Acha o primeiro
	//bloco vazio
	if(preenche_bloco (path, DIREITOS_PADRAO, 0, NULL, S_IFREG) == 0) {
		return 0;
  }
	return ENOSPC;
}

// Cria um diretório no caminho apontado por path
static int mkdir_brisafs(const char *path, mode_t type){
	if(preenche_bloco (path, DIREITOS_PADRAO, 0, NULL, S_IFDIR) == 0) {
		return 0;
	}
  return ENOSPC;
}

// Release de um arquivo
static int release_brisafs(const char *path, struct fuse_file_info *fi) {
	salva_disco();
  return 0;
}

/* Esta estrutura contém os ponteiros para as operações implementadas
   no FS */
static struct fuse_operations fuse_brisafs = {
                                              .create = create_brisafs,
                                              .fsync = fsync_brisafs,
                                              .getattr = getattr_brisafs,
                                              .mknod = mknod_brisafs,
                                              .open = open_brisafs,
                                              .read = read_brisafs,
                                              .readdir = readdir_brisafs,
                                              .truncate	= truncate_brisafs,
                                              .utimens = utimens_brisafs,
                                              .write = write_brisafs,
                                              .chown = chown_brisafs,
                                              .release = release_brisafs,
                                              .mkdir = mkdir_brisafs,
                                              .unlink = unlink_brisafs,
                                              .rmdir = rmdir_brisafs,
                                              .chmod = chmod_brisafs
};

int main(int argc, char *argv[]) {

	printf("Iniciando o BrisaFS...\n");
	printf("\t Tamanho do bloco = %d bytes\n", TAM_BLOCO);
  printf("\t Tamanho máximo de arquivo = %d bytes\n", MAX_FILE_SIZE);
  printf("\t Tamanho do inode: %lu bytes\n", sizeof(inode));
  printf("\t Quantidade de inodes: %u\n", MIN_DATABLOCKS);
  printf("\t Número máximo de inodes por superboco: %lu\n", MAX_FILES);
  printf("\t Número de superbocos: %lu\n", N_SUPERBLOCKS);
  printf("\t Quantidade de blocos no disco: %lu\n", MAX_BLOCOS);
  printf("\t Tamanho do Disco: %lu bytes\n", TAM_BLOCO * MAX_BLOCOS);

  init_brisafs();

  return fuse_main(argc, argv, &fuse_brisafs, NULL);
}
