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
    char nome[250]; // 250 bytes
    mode_t type; // 4 bytes
    uint32_t timestamp[2]; //0: Modificacao, 1: Acesso
    uint16_t direitos;
    uint16_t tamanho;
    uint16_t bloco;
		uint16_t proxbloco;
    uid_t userown; // 4 bytes
    gid_t groupown; // 4 bytes
    //char parent[250];
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
        if (result != lSize)
          fputs ("Reading error ", stderr);
        return 1;
    } else
        return 0;
}

/* Preenche os campos do superbloco de índice isuperbloco */
/*
void preenche_bloco (const char *nome, uint16_t direitos, uint16_t tamanho, 
										 const char *conteudo, mode_t type) {
  
	int num_blocos = (1+((tamanho-1) / TAM_BLOCO));
	printf("Blocos necessários para o arquivo: %d\n", num_blocos);
	
	if (tamanho > MAX_FILE_SIZE) {
		printf("Tamanho máximo de arquivo excedido!\n");
		return;
		
	} else if (num_blocos > free_space) {
		printf("Não há espaço suficiente em disco para este arquivo!\n");
		return;
		
	} else {
		int bloco_anterior;
		
		char *mnome = NULL;
    char *pai = NULL;
    
    quebra_nome(nome, &mnome, &pai);
    
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
    	if (superbloco[i].bloco != 0 && compara_nome (pai, superbloco[i].parent)
				  && compara_nome (mnome, superbloco[i].nome)) {
				printf("O arquivo %s já existe!\n", mnome);
				return;
			}
    }
			
		for (int k = 0; k < num_blocos; k++) {
			printf("k = %d\n", k);
			printf("N_SUPERBLOCKS = %lu\n", N_SUPERBLOCKS);		
			for (int i = 0; i < N_SUPERBLOCKS; i++) {
      	if (superbloco[i].bloco == 0) { //bloco vazio
      		printf("Inode vazio: %d\n", i);
      		printf("Bloco vazio: %ld\n", N_SUPERBLOCKS + i + 1);
        	if(k == 0) {
						superbloco[i].id = i;
    				strcpy(superbloco[i].nome, mnome);
    				superbloco[i].direitos = direitos;
    				superbloco[i].tamanho = tamanho;
    				superbloco[i].bloco = N_SUPERBLOCKS + i + 1;
    				superbloco[i].type = type;
    				strcpy(superbloco[i].parent, pai);
    				armazena_data (0, i);

						superbloco[i].proxbloco = 0;
						bloco_anterior = i;

    				free(mnome);
    				free(pai);

    				if (conteudo != NULL) {
    					printf("Conteúdo: %s\n", conteudo);
    					printf("Tamanho do conteúdo = %lu bytes\n", sizeof(conteudo));
							if (sizeof(conteudo) > TAM_BLOCO)
      	 				memcpy(disco + DISCO_OFFSET(N_SUPERBLOCKS + i + 1), conteudo, TAM_BLOCO);
							else
								memcpy(disco + DISCO_OFFSET(N_SUPERBLOCKS + i + 1), conteudo, tamanho);
    				} else {
     	  			memset(disco + DISCO_OFFSET(N_SUPERBLOCKS + i + 1), 0, tamanho);
						}
						
						free_space--;
						break;

					} else {
						superbloco[i].id = i;
    				//strcpy(superbloco[i].nome, mnome);
    				//superbloco[i].direitos = direitos;
   					//superbloco[i].tamanho = tamanho;
    				superbloco[i].bloco = N_SUPERBLOCKS + i + 1;
    				superbloco[i].type = type;
    				//strcpy(superbloco[i].parent, pai);
    				//armazena_data (0, i);
						
						superbloco[i].proxbloco = 0;
						superbloco[bloco_anterior].proxbloco = i;
						bloco_anterior = i;

						if (sizeof(conteudo) > TAM_BLOCO)
      	  		memcpy(disco + DISCO_OFFSET(N_SUPERBLOCKS + i + 1), 
								conteudo + DISCO_OFFSET(k), TAM_BLOCO);
						else
							memcpy(disco + DISCO_OFFSET(N_SUPERBLOCKS + i + 1), 
								conteudo + DISCO_OFFSET(k), tamanho);
						
						free_space--;
						break;
					}
        }
			}
		}
	}
}
*/

/* Preenche os campos do superbloco de índice isuperbloco */
int preenche_bloco (const char *nome, uint16_t direitos, uint16_t tamanho, 
											const byte *conteudo, mode_t type) {
    
	for (int isuperbloco = 0; isuperbloco < N_SUPERBLOCKS; isuperbloco++) {
  	if (superbloco[isuperbloco].bloco == 0) {//ninguem usando
    
    	uint16_t bloco = N_SUPERBLOCKS + isuperbloco + 1;
    
    	char *mnome = NULL;
    	char *pai = NULL;

    	quebra_nome(nome, &mnome, &pai);

    	superbloco[isuperbloco].id = isuperbloco;
    	strcpy(superbloco[isuperbloco].nome, mnome);
    	superbloco[isuperbloco].direitos = direitos;
    	superbloco[isuperbloco].tamanho = tamanho;
    	superbloco[isuperbloco].bloco = bloco;
    	superbloco[isuperbloco].type = type;
    	//strcpy(superbloco[isuperbloco].parent, pai);
    	armazena_data (0, isuperbloco);
    
    	// Se for um diretório, inicializa informando que está vazio
    	if (type == S_IFDIR) {
    		dir = disco + DISCO_OFFSET(bloco);
    		uint16_t *d = (uint16_t*) dir;
    		d[0] = 0;
    	} else {
    		if (conteudo != NULL)
        	memcpy(disco + DISCO_OFFSET(bloco), conteudo, tamanho);
    		else
        	memset(disco + DISCO_OFFSET(bloco), 0, tamanho);
    	}
    
    	/* Para qualquer um, que não o root, procura o inode do diretório pai e 
    	 	 informa a própria criação */
    	if (strcmp(mnome,"/")!=0) {
    		for (int i = 0; i < N_SUPERBLOCKS; i++) {
    			if (compara_nome(pai, superbloco[i].nome)) { //Achou o diretório pai
    				dir = disco + DISCO_OFFSET(superbloco[i].bloco);
    				uint16_t *d = (uint16_t*) dir;
    				d[0]++;
    				d[d[0]] = isuperbloco;
    				break;
    			}
    		}
    	}
    	free(mnome);
    	free(pai);
    	return 0;
    }
	}
	return 1;
}

/*
void preenche_bloco_dir (int isuperbloco, const char *nome, uint16_t direitos,
                     uint16_t tamanho, uint16_t bloco, dirbloco *conteudo,
                     mode_t type) {
    char *mnome = NULL;
    char *pai = NULL;

    quebra_nome(nome, &mnome, &pai);

    superbloco[isuperbloco].id = isuperbloco;
    strcpy(superbloco[isuperbloco].nome, mnome);
    superbloco[isuperbloco].direitos = direitos;
    superbloco[isuperbloco].tamanho = tamanho;
    superbloco[isuperbloco].bloco = bloco;
    superbloco[isuperbloco].type = type;
    strcpy(superbloco[isuperbloco].parent, pai);
    armazena_data(0, isuperbloco);

    free(mnome);
    free(pai);

    if (conteudo != NULL)
        memcpy(disco + DISCO_OFFSET(bloco), conteudo, tamanho);
    else
        memset(disco + DISCO_OFFSET(bloco), 0, tamanho);

}
*/

/* Para persistir o FS em um disco representado por um arquivo, talvez
   seja necessário "formatar" o arquivo pegando o seu tamanho e
   inicializando todas as posições (ou apenas o(s) superbloco(s))
   com os valores apropriados */
void init_brisafs() {
  disco = calloc (MAX_BLOCOS, TAM_BLOCO);
  superbloco = (inode*) disco; //posição 0
  dir = (byte*) disco; //posição 0

  if(carrega_disco() == 0){
    //Cria o diretório raiz
    preenche_bloco ("/", DIREITOS_PADRAO, 64, NULL, S_IFDIR);
    //Cria um arquivo na mão de boas vindas caso nao haja disco
    char *nome = "/BrisaFS.txt";
    //Cuidado! pois se tiver acentos em UTF8 uma letra pode ser mais que um byte
    char *conteudo = "Adoro as aulas de SO da UFABC!\n";
    //0 está sendo usado pelo superbloco. O primeiro livre é o posterior ao N_SUPERBLOCKS
    //preenche_bloco(1, nome, DIREITOS_PADRAO, strlen(conteudo),
    //N_SUPERBLOCKS + 2, (byte*)conteudo, S_IFREG);
    preenche_bloco(nome, DIREITOS_PADRAO, strlen(conteudo), (byte*) conteudo, S_IFREG);
  }
}

/* Recebe o path de um arquivo e retorna o nome do arquivo e o diretorio pai*/
int quebra_nome (const char *path, char **name, char **parent) {
  char* c1 = strdup(path);
	char* c2 = strdup(path);
	
	*parent = (char*)malloc(sizeof(char)*strlen(c1));
  *name = (char*)malloc(sizeof(char)*strlen(c1));
	if (parent == NULL || name == NULL)
		return 1;
	
	char* n = basename(c1);
	char* p = basename(dirname(c2));
	if (p == NULL || n == NULL)
		return 2;

	strcpy(*name, n);
	strcpy(*parent, p);

  return 0;
}

/* Devolve 1 caso representem o mesmo nome e 0 cc */
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

int compara_pai (const char *path, const char *nome) {
    char *mn = NULL;
    char *mp = NULL;

    quebra_nome(path, &mn, &mp);
    
    printf("Pai Calculado: %s\n", mp);
    printf("Pai Comparado: %s\n", nome);

		if (strcmp(nome, mp) == 0) {
			free(mn);
			free(mp);
			return 1;
		}
		
		free(mn);
		free(mp);
    return 0;
}

int armazena_data (int typeop, int inode){
    struct timeval time;
    gettimeofday (&time, NULL);

    if (typeop == 0) { //modificacao
      superbloco[inode].timestamp[0] = time.tv_sec;
      superbloco[inode].timestamp[1] = time.tv_sec;

        return 0;
    } else if (typeop == 1) {
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

    //Busca arquivo na lista de inodes
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco != 0 //Bloco sendo usado
            && compara_nome (path, superbloco[i].nome)) { //Nome bate

            stbuf->st_mode = superbloco[i].type | superbloco[i].direitos;
            stbuf->st_nlink = 1;
            stbuf->st_size = superbloco[i].tamanho;
            stbuf->st_mtime = superbloco[i].timestamp[0];
            stbuf->st_atime = superbloco[i].timestamp[1];
            stbuf->st_uid = superbloco[i].userown;
            stbuf->st_gid = superbloco[i].groupown;
            return 0; //OK, arquivo encontrado
        }
    }
    //Erro arquivo não encontrado
    return -ENOENT;
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
		
		for (int i = 0; i < N_SUPERBLOCKS; i++) {
    	if (superbloco[i].bloco != 0 
    				&& compara_nome (path, superbloco[i].nome)) { //Achou o diretório pai
    		dir = disco + DISCO_OFFSET(superbloco[i].bloco);
    		uint16_t *d = (uint16_t*) dir;
    		for(int j = 1; j <= d[0]; j++) {
    			filler(buf, superbloco[d[j]].nome, NULL, 0);
    		}
    	}
    }
    			
		/*
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco != 0
						&& compara_nome (path, superbloco[i].parent)) { //Bloco ocupado!
            filler(buf, superbloco[i].nome, NULL, 0);
        }
    }
		*/
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
		
	//A partir do path, encontra o pai do arquivo.
  for (int i = 0; i < N_SUPERBLOCKS; i++) {
  	if (superbloco[i].bloco != 0 
    		&& compara_pai (path, superbloco[i].nome)) { //Achou o diretório pai
    	dir = disco + DISCO_OFFSET(superbloco[i].bloco);
    	uint16_t *d = (uint16_t*) dir;
    	// Varre todos os arquivos dentro do pai para encontrar o arquivo
    	for(int j = 1; j <= d[0]; j++) {
    		if (superbloco[d[j]].bloco == 0) //bloco vazio
          continue;
        if (compara_nome(path, superbloco[d[j]].nome)) {//achou!
        	printf("Caminho: %s\n", path);
        	printf("Buf: %s\n", buf);
        	printf("Tamanho: %ld\n", size);
        	printf("Offset: %ld\n", offset);
        	printf("To lendo o inode %u referente ao bloco %u\n", 
        		superbloco[d[j]].id, superbloco[d[j]].bloco);
          size_t len = superbloco[d[j]].tamanho;
          printf("len: %ld\n", len);
          armazena_data(1, d[j]);
          if (offset >= len) {//tentou ler além do fim do arquivo
            printf("Tentou ler o fim do arquivo\n");
            return 0;
          }
          if (offset + size > len) {
            printf("offset + size maior que len\n");
            memcpy(buf, disco + DISCO_OFFSET(superbloco[d[j]].bloco), len - offset);
            printf("Buf: %s\n", buf);
            return len - offset;
          }
          
          memcpy(buf, disco + DISCO_OFFSET(superbloco[d[j]].bloco), size);
          printf("Buf: %s\n", buf);
          return size;
        }
    	}
    	break;
    }
  }
    
    
		
    //Procura o arquivo
    /*
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) //bloco vazio
            continue;
        if (compara_nome(path, superbloco[i].nome)
        		&& compara_pai (path, superbloco[i].parent)) {//achou!
        		printf("Caminho: %s\n", path);
        		printf("Buf: %s\n", buf);
        		printf("Tamanho: %ld\n", size);
        		printf("Offset: %ld\n", offset);
        		printf("To lendo o inode %u referente ao bloco %u\n", 
        			superbloco[i].id, superbloco[i].bloco);
            size_t len = superbloco[i].tamanho;
            printf("len: %ld\n", len);
            armazena_data(1, i);
            if (offset >= len) {//tentou ler além do fim do arquivo
            		printf("Tentou ler o fim do arquivo\n");
                return 0;
            }
            if (offset + size > len) {
            		printf("offset + size maior que len\n");
                memcpy(buf, disco + DISCO_OFFSET(superbloco[i].bloco),
                       len - offset);
                printf("Buf: %s\n", buf);
                return len - offset;
            }

            memcpy(buf, disco + DISCO_OFFSET(superbloco[i].bloco), size);
            printf("Buf: %s\n", buf);
            return size;
        }
    }
    */
    //Arquivo não encontrado
    return -ENOENT;
}

/* Função chamada quando o FUSE deseja escrever dados em um arquivo
   indicado pelo parâmetro path. Se você implementou a função
   open_brisafs, o uso do parâmetro fi é necessário. A função escreve
   size bytes, a partir do offset do arquivo path no buffer buf. */
static int write_brisafs(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {

  //A partir do path, encontra o pai do arquivo.
  for (int i = 0; i < N_SUPERBLOCKS; i++) {
  	if (superbloco[i].bloco != 0 
    		&& compara_pai (path, superbloco[i].nome)) { //Achou o diretório pai
    	dir = disco + DISCO_OFFSET(superbloco[i].bloco);
    	uint16_t *d = (uint16_t*) dir;
    	// Varre todos os arquivos dentro do pai para encontrar o arquivo
    	for(int j = 1; j <= d[0]; j++) {
    		if (superbloco[d[j]].bloco == 0) { //bloco vazio
          continue;
        }
        if (compara_nome(path, superbloco[d[j]].nome)) {//achou!
        	// Cuidado! Não checa se a quantidade de bytes cabe no arquivo!
        	printf("Caminho: %s\n", path);
          printf("Buf: %s\n", buf);
        	printf("Tamanho: %ld\n", size);
        	printf("Offset: %ld\n", offset);
        	printf("To escevendo o inode %u referente ao bloco %u\n\n", 
        			superbloco[d[j]].id, superbloco[d[j]].bloco);
          memcpy(disco + DISCO_OFFSET(superbloco[d[j]].bloco) + offset, buf, size);
          superbloco[d[j]].tamanho = offset + size;
          armazena_data(0, d[j]);
          return size;
        }
    	}
    	break;
    }
  }
    
    /*
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) { //bloco vazio
            continue;
        }
        if (compara_nome(path, superbloco[i].nome)
        		&& compara_pai (path, superbloco[i].parent)) {//achou!
            // Cuidado! Não checa se a quantidade de bytes cabe no arquivo!
            printf("Caminho: %s\n", path);
            printf("Buf: %s\n", buf);
        		printf("Tamanho: %ld\n", size);
        		printf("Offset: %ld\n", offset);
        		printf("To escevendo o inode %u referente ao bloco %u\n\n", 
        			superbloco[i].id, superbloco[i].bloco);
            memcpy(disco + DISCO_OFFSET(superbloco[i].bloco) + offset, buf, size);
            superbloco[i].tamanho = offset + size;
            armazena_data(0, i);
            return size;
        }
    }
    */
    //Se chegou aqui não achou. Entao cria
    //Acha o primeiro bloco vazio
    preenche_bloco (path, DIREITOS_PADRAO, size, buf, S_IFREG);
    return size;
    /*
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) {//ninguem usando
            preenche_bloco (i, path, DIREITOS_PADRAO, size, N_SUPERBLOCKS +
              i + 1, buf, S_IFREG);
            armazena_data(0, i);
            return size;
        }
    }
    */

    return -EIO;
}

// Remove arquivo
static int unlink_brisafs(const char *path) {

  //A partir do path, encontra o pai do arquivo.
  for (int i = 0; i < N_SUPERBLOCKS; i++) {
  	if (superbloco[i].bloco != 0 
    		&& compara_pai (path, superbloco[i].nome)) { //Achou o diretório pai
    	dir = disco + DISCO_OFFSET(superbloco[i].bloco);
    	uint16_t *d = (uint16_t*) dir;
    	// Varre todos os arquivos dentro do pai para encontrar o arquivo
    	for(int j = 1; j <= d[0]; j++) {
    		if (superbloco[d[j]].bloco == 0) //bloco vazio
      		continue;
    		if (compara_nome(path, superbloco[d[j]].nome)) {//achou!
      		// Informa que o bloco está disponível para gravação
      		superbloco[d[j]].bloco = 0;
      		superbloco[d[j]].tamanho = 0;
      		
      		// Remove o arquivo do diretório pai
      		d[0]--;
      		for(int w = j; j <= d[0]; j++) {
      			d[w] = d[w+1];
      		}
      		return 0;
      	}
    	}
    	break;
    }
  }
  //Arquivo não encontrado
  return -ENOENT;
}

/* Altera o tamanho do arquivo apontado por path para tamanho size
   bytes */
static int truncate_brisafs(const char *path, off_t size) {
  if (size > TAM_BLOCO) {
  	return EFBIG;
	}
	
	int findex = -1;
	//A partir do path, encontra o pai do arquivo.
  for (int i = 0; i < N_SUPERBLOCKS; i++) {
  	if (superbloco[i].bloco != 0 
    		&& compara_pai (path, superbloco[i].nome)) { //Achou o diretório pai
    	dir = disco + DISCO_OFFSET(superbloco[i].bloco);
    	uint16_t *d = (uint16_t*) dir;
    	// Varre todos os arquivos dentro do pai para encontrar o arquivo
    	for(int j = 1; j <= d[0]; j++) {
    		findex = i;
        break;
    	}
    	break;
    }
  }  	
	
    //procura o arquivo
    /*
    int findex = -1;
    for(int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco != 0
            && compara_nome(path, superbloco[i].nome)
            && compara_pai (path, superbloco[i].parent)) {
            findex = i;
            break;
        }
    }
    */
    if (findex != -1) {// arquivo existente
        superbloco[findex].tamanho = size;
        return 0;
    } else {// Arquivo novo
        //Acha o primeiro bloco vazio
        preenche_bloco (path, DIREITOS_PADRAO, size, NULL, S_IFREG);
        /*
        for (int i = 0; i < N_SUPERBLOCKS; i++) {
            if (superbloco[i].bloco == 0) {//ninguem usando
                preenche_bloco (i, path, DIREITOS_PADRAO, size, N_SUPERBLOCKS +
                  i + 1, NULL, S_IFREG);
                break;
            }
        }
        */
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
        preenche_bloco (path, DIREITOS_PADRAO, 0, NULL, S_IFREG);
        return 0;
        /*
        for (int i = 0; i < N_SUPERBLOCKS; i++) {
            if (superbloco[i].bloco == 0) {//ninguem usando
                preenche_bloco (i, path, DIREITOS_PADRAO, 0, N_SUPERBLOCKS +
                  i + 1, NULL,S_IFREG);
                return 0;
            }
        }
        */
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
    
  //A partir do path, encontra o pai do arquivo.
  for (int i = 0; i < N_SUPERBLOCKS; i++) {
  	if (superbloco[i].bloco != 0 
    		&& compara_pai (path, superbloco[i].nome)) { //Achou o diretório pai
    	dir = disco + DISCO_OFFSET(superbloco[i].bloco);
    	uint16_t *d = (uint16_t*) dir;
    	// Varre todos os arquivos dentro do pai para encontrar o arquivo
    	for(int j = 1; j <= d[0]; j++) {
    		if (superbloco[d[j]].bloco == 0) //bloco vazio
        	continue;
        if (compara_nome(path, superbloco[d[j]].nome)) {//achou!
        	if(userowner != -1)
          	superbloco[d[j]].userown = userowner;

          if(groupowner != -1)
          	superbloco[d[j]].groupown = groupowner;

          return 0;
        }
    	}
    break;
    }
  }
    /*
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) //bloco vazio
            continue;
        if (compara_nome(path, superbloco[i].nome)
        		&& compara_pai (path, superbloco[i].parent)) {//achou!
            if(userowner != -1)
                superbloco[i].userown = userowner;

            if(groupowner != -1)
                superbloco[i].groupown = groupowner;

            return 0;
        }
    }
    */
    //Arquivo não encontrado
    return -ENOENT;
}


/* Cria e abre o arquivo apontado por path. Se o arquivo não existir
   cria e depois abre*/
static int create_brisafs(const char *path, mode_t mode,
                          struct fuse_file_info *fi) {
    //Cuidado! Está ignorando todos os parâmetros. O seu deverá
    //cuidar disso Veja "man 2 mknod" para instruções de como pegar os
    //direitos e demais informações sobre os arquivos Acha o primeiro
    //bloco vazio
    preenche_bloco (path, DIREITOS_PADRAO, 64, NULL, S_IFREG);
    return 0;
    /*
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) {//ninguem usando
            preenche_bloco (i, path, DIREITOS_PADRAO, 64, N_SUPERBLOCKS +
              i + 1, NULL, S_IFREG);
            return 0;
        }
    }
    */
    return ENOSPC;
}

static int mkdir_brisafs(const char *path, mode_t type){
    preenche_bloco (path, DIREITOS_PADRAO, 64, NULL, S_IFDIR);
    return 0;
    /*
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) {//ninguem usando
            preenche_bloco (i, path, DIREITOS_PADRAO, 64, N_SUPERBLOCKS +
              i + 1, NULL, S_IFDIR);
            return 0;
        }
    }
    */
    return ENOSPC;
}


static int chmod_brisafs(const char *path, mode_t mode) {
	
	//A partir do path, encontra o pai do arquivo.
  for (int i = 0; i < N_SUPERBLOCKS; i++) {
  	if (superbloco[i].bloco != 0 
    		&& compara_pai (path, superbloco[i].nome)) { //Achou o diretório pai
    	dir = disco + DISCO_OFFSET(superbloco[i].bloco);
    	uint16_t *d = (uint16_t*) dir;
    	// Varre todos os arquivos dentro do pai para encontrar o arquivo
    	for(int j = 1; j <= d[0]; j++) {
    		if (compara_nome(path, superbloco[d[j]].nome)) {//achou!
    			superbloco[d[j]].direitos = mode;
    		return 0;
    		}
    	}
    break;
    }
  }
	/*
	for (int i = 0; i < N_SUPERBLOCKS; i++) {
    if (compara_nome(path, superbloco[i].nome)
    		&& compara_pai (path, superbloco[i].parent)) {//achou!
    	superbloco[i].direitos = mode;
    	return 0;
    }
	}
	*/
	return -errno;
}


static int release_brisafs(const char *path, struct fuse_file_info *fi){
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
