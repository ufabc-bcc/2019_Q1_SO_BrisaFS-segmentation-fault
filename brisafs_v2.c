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

/* Número máximo de arquivos */
#define N_FILES 1024
/* Tamanho máximo de um arquivo */
#define MAX_FILE_SIZE 64000000
/* Tamanho do bloco do dispositivo */
#define TAM_BLOCO 4096

/* Quantidade de arquivos por bloco */
#define MAX_FILES (TAM_BLOCO / sizeof(inode))

/* Quantidade mínima de superblocks para comportar todos os inodes */
/* Forçar o arredonamento para cima: q = x/y = 1+((x-1)/y) */
#define N_SUPERBLOCKS 1+((N_FILES-1) / MAX_FILES)

/* Quantidade mínima de blocos para armazenar um arquivo de tamanho MAX_FILE_SIZE*/
/* Forçar o arredonamento para cima: q = x/y = 1+((x-1)/y) */
#define MIN_DATABLOCKS 1+((MAX_FILE_SIZE-1) / TAM_BLOCO)

/* Quantidade de blocos necessário para a tabela FAT */
/* Forçar o arredonamento para cima: q = x/y = 1+((x-1)/y) */
#define FAT_BLOCOS  1+((MIN_DATABLOCKS-1) / (TAM_BLOCO / sizeof(uint16_t)))

/* Total de blocos necessários para o sistema de arquivos */
#define MAX_BLOCOS (N_SUPERBLOCKS + FAT_BLOCOS + MIN_DATABLOCKS)

/* Direitos -rw-r--r-- */
#define DIREITOS_PADRAO 0644

typedef char byte;

/*Estrutura caso o arquivo seja um diretorio*/
typedef struct{
    uint16_t id;
    char nome[250];
}dirbloco;

/* Um inode guarda todas as informações relativas a um arquivo como
   por exemplo nome, direitos, tamanho, bloco inicial, ... */
typedef struct {
    uint16_t id;
    char nome[250];
    mode_t type;
    uint32_t timestamp[2]; //0: Modificacao, 1: Acesso
    uint16_t direitos;
    uint16_t tamanho;
    uint16_t bloco;
    uid_t userown;
    gid_t groupown;
    char parent[250];
} inode;

/* Disco - A variável abaixo representa um disco que pode ser acessado
   por blocos de tamanho TAM_BLOCO com um total de MAX_BLOCOS. */
byte *disco;

//guarda os inodes dos arquivos
inode *superbloco;

#define DISCO_OFFSET(B) (B * TAM_BLOCO)

//cabecalho funcao
int armazena_data(int typeop, int inode);
int quebra_nome (const char *path, char **name, char **parent);


void salva_disco(){
    FILE *file  = fopen ("hdd1", "wb");
    fwrite (disco, TAM_BLOCO, MAX_BLOCOS, file);
    printf ("Salvando arquivo HDD\n");
    fclose (file);
}

int carrega_disco() {
    size_t result;
    size_t lSize;
    if (access("hdd1", F_OK) == 0){
        FILE *file = fopen ("hdd1", "rb");
        lSize = ftell (file);
        result = fread (disco,TAM_BLOCO,MAX_BLOCOS,file);
        fclose (file);
        printf ("Carregou\n");
        if (result != lSize)
          fputs ("Reading error", stderr);
        return 1;
    } else
        return 0;
}

/* Preenche os campos do superbloco de índice isuperbloco */
void preenche_bloco (int isuperbloco, const char *nome, uint16_t direitos,
                     uint16_t tamanho, uint16_t bloco, const byte *conteudo,
                     mode_t type) {
    char *mnome = NULL;
    char *pai = NULL;
    //Joga fora a(s) barras iniciais
    //while (mnome[0] != '\0' && mnome[0] == '/')
    //    mnome++;

    quebra_nome(nome, &mnome, &pai);

    superbloco[isuperbloco].id = isuperbloco;
    strcpy(superbloco[isuperbloco].nome, mnome);
    superbloco[isuperbloco].direitos = direitos;
    superbloco[isuperbloco].tamanho = tamanho;
    superbloco[isuperbloco].bloco = bloco;
    superbloco[isuperbloco].type = type;
    strcpy(superbloco[isuperbloco].parent, pai);
    armazena_data (0, isuperbloco);

    free(mnome);
    free(pai);

    if (conteudo != NULL)
        memcpy(disco + DISCO_OFFSET(bloco), conteudo, tamanho);
    else
        memset(disco + DISCO_OFFSET(bloco), 0, tamanho);
}

void preenche_bloco_dir (int isuperbloco, const char *nome, uint16_t direitos,
                     uint16_t tamanho, uint16_t bloco, dirbloco *conteudo,
                     mode_t type) {
    char *mnome = NULL;
    char *pai = NULL;
    //Joga fora a(s) barras iniciais
    //while (mnome[0] != '\0' && mnome[0] == '/')
    //    mnome++;

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

/* Para persistir o FS em um disco representado por um arquivo, talvez
   seja necessário "formatar" o arquivo pegando o seu tamanho e
   inicializando todas as posições (ou apenas o(s) superbloco(s))
   com os valores apropriados */
void init_brisafs() {
    disco = calloc (MAX_BLOCOS, TAM_BLOCO);
    superbloco = (inode*) disco; //posição 0

    if(carrega_disco() == 0){
        //Cria um arquivo na mão de boas vindas caso nao haja disco
        char *nome = "UFABC SO 2019.txt";
        //Cuidado! pois se tiver acentos em UTF8 uma letra pode ser mais que um byte
        char *conteudo = "Adoro as aulas de SO da UFABC!\n";
        //0 está sendo usado pelo superbloco. O primeiro livre é o posterior ao N_SUPERBLOCKS e ao FAT_BLOCKS
        preenche_bloco(0, nome, DIREITOS_PADRAO, strlen(conteudo),
          N_SUPERBLOCKS + FAT_BLOCOS + 1, (byte*)conteudo, S_IFREG);
    }
}

int quebra_nome (const char *path, char **name, char **parent) {
  char str[strlen(path)];
  char *p;
  char *n;
  char *aux;

  *parent = (char*)malloc(sizeof(char)*strlen(path));
  *name = (char*)malloc(sizeof(char)*strlen(path));

  strcpy(str, path);
  p = "/";
  n = strtok (str,"/");

  while (1) {
    aux = n;
    n = strtok (NULL, "/");
    if(n != NULL)
      p = aux;
    else {
      n = aux;
      break;
    }
  }

  strcpy(*name, n);
  strcpy(*parent, p);

  return 0;
}

/* Devolve 1 caso representem o mesmo nome e 0 cc */
int compara_nome (const char *a, const char *b) {
    char *ma = (char*)a;
    char *mb = (char*)b;
    //Joga fora barras iniciais
    while (ma[0] != '\0' && ma[0] == '/')
        ma++;
    while (mb[0] != '\0' && mb[0] == '/')
        mb++;
    //Cuidado! Pode ser necessário jogar fora também barras repetidas internas
    //quando tiver diretórios
    return strcmp(ma, mb) == 0;
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
            && compara_nome(superbloco[i].nome, path)) { //Nome bate

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
        if (superbloco[i].bloco != 0) { //Bloco ocupado!
            filler(buf, superbloco[i].nome, NULL, 0);
        }
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

    //Procura o arquivo
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) //bloco vazio
            continue;
        if (compara_nome(path, superbloco[i].nome)) {//achou!
            size_t len = superbloco[i].tamanho;
            armazena_data(1, i);
            if (offset >= len) {//tentou ler além do fim do arquivo
                return 0;
            }
            if (offset + size > len) {
                memcpy(buf,
                       disco + DISCO_OFFSET(superbloco[i].bloco),
                       len - offset);
                return len - offset;
            }

            memcpy(buf,
                   disco + DISCO_OFFSET(superbloco[i].bloco), size);
            return size;
        }
    }
    //Arquivo não encontrado
    return -ENOENT;
}

/* Função chamada quando o FUSE deseja escrever dados em um arquivo
   indicado pelo parâmetro path. Se você implementou a função
   open_brisafs, o uso do parâmetro fi é necessário. A função escreve
   size bytes, a partir do offset do arquivo path no buffer buf. */
static int write_brisafs(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi) {

    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) { //bloco vazio
            continue;
        }
        if (compara_nome(path, superbloco[i].nome)) {//achou!
            // Cuidado! Não checa se a quantidade de bytes cabe no arquivo!
            memcpy(disco + DISCO_OFFSET(superbloco[i].bloco) + offset, buf, size);
            superbloco[i].tamanho = offset + size;
            armazena_data(0, i);
            return size;
        }
    }
    //Se chegou aqui não achou. Entao cria
    //Acha o primeiro bloco vazio
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) {//ninguem usando
            preenche_bloco (i, path, DIREITOS_PADRAO, size, N_SUPERBLOCKS +
              FAT_BLOCOS + i + 1, buf,S_IFREG);
            armazena_data(0, i);
            return size;
        }
    }

    return -EIO;
}

// Remove arquivo
static int unlink_brisafs(const char *path) {

  //Procura o arquivo
  for (int i = 0; i < N_SUPERBLOCKS; i++) {
    if (superbloco[i].bloco == 0) //bloco vazio
      continue;
    if (compara_nome(path, superbloco[i].nome)) {//achou!
      superbloco[i].bloco = 0;
      superbloco[i].tamanho = 0;
      return 0;
    }
  }
  //Arquivo não encontrado
  return -ENOENT;
}

/* Altera o tamanho do arquivo apontado por path para tamanho size
   bytes */
static int truncate_brisafs(const char *path, off_t size) {
    if (size > TAM_BLOCO)
        return EFBIG;

    //procura o arquivo
    int findex = -1;
    for(int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco != 0
            && compara_nome(path, superbloco[i].nome)) {
            findex = i;
            break;
        }
    }
    if (findex != -1) {// arquivo existente
        superbloco[findex].tamanho = size;
        return 0;
    } else {// Arquivo novo
        //Acha o primeiro bloco vazio
        for (int i = 0; i < N_SUPERBLOCKS; i++) {
            if (superbloco[i].bloco == 0) {//ninguem usando
                preenche_bloco (i, path, DIREITOS_PADRAO, size, N_SUPERBLOCKS +
                  FAT_BLOCOS + i + 1, NULL, S_IFREG);
                break;
            }
        }
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
        for (int i = 0; i < N_SUPERBLOCKS; i++) {
            if (superbloco[i].bloco == 0) {//ninguem usando
                preenche_bloco (i, path, DIREITOS_PADRAO, 0, N_SUPERBLOCKS +
                  FAT_BLOCOS + i + 1, NULL,S_IFREG);
                return 0;
            }
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
    printf("O GRUPO EH: %d\n", groupowner);
    printf("O USUARIO EH: %d\n", userowner);
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) //bloco vazio
            continue;
        if (compara_nome(path, superbloco[i].nome)) {//achou!
            if(userowner != -1)
                superbloco[i].userown = userowner;

            if(groupowner != -1)
                superbloco[i].groupown = groupowner;

            return 0;
        }
    }
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
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) {//ninguem usando
            preenche_bloco (i, path, DIREITOS_PADRAO, 64, N_SUPERBLOCKS +
              FAT_BLOCOS + i + 1, NULL,S_IFREG);
            return 0;
        }
    }
    return ENOSPC;
}

static int mkdir_brisafs(const char *path, mode_t type){
    for (int i = 0; i < N_SUPERBLOCKS; i++) {
        if (superbloco[i].bloco == 0) {//ninguem usando
            preenche_bloco_dir (i, path, DIREITOS_PADRAO, 64, N_SUPERBLOCKS +
              FAT_BLOCOS + i + 1, NULL,S_IFDIR);
            return 0;
        }
    }
    return ENOSPC;
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
                                              .unlink = unlink_brisafs
};

int main(int argc, char *argv[]) {

    printf("Iniciando o BrisaFS...\n");
    printf("\t Tamanho máximo de arquivo = 1 bloco = %d bytes\n", TAM_BLOCO);
    printf("\t Tamanho do inode: %lu\n", sizeof(inode));
    printf("\t Número máximo de arquivos por superboco: %lu\n", MAX_FILES);
    printf("\t Número máximo de superbocos: %lu\n", N_SUPERBLOCKS);
    printf("\t Número máximo de arquivos: %lu\n", N_SUPERBLOCKS * MAX_FILES);
    printf("\t Tamanho do Disco %lu\n", sizeof(disco) * MAX_BLOCOS );

    init_brisafs();

    return fuse_main(argc, argv, &fuse_brisafs, NULL);
}
