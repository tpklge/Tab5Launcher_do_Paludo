#include "firmware_parser.h"
#include <stdio.h>
#include <string.h>
static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1]<<8; }
#define REJECT(msg) do { if (error && error_size) snprintf(error,error_size,"%s",msg); return false; } while (0)
bool firmware_parse_chip(void *ctx, fw_read_fn read, fw_hash_fn hash, uint32_t size, uint16_t chip,
                    uint16_t revision, firmware_image_t *out, char *error, size_t error_size)
{
    if (!read || !out) REJECT("Argumento invalido");
    memset(out,0,sizeof(*out));
    if (error && error_size) error[0]=0;
    if (size<24+8+256+1 || size>FW_MAX_FILE_SIZE) REJECT("Tamanho de imagem invalido");
    uint8_t header[24];
    if (!read(ctx,0,header,sizeof(header))) REJECT("Erro ao ler cabecalho");
    if (header[0]!=0xE9) REJECT("Nao e app-only ESP. Merged/backup/pacote ainda nao instalavel");
    if (!header[1] || header[1]>16 || header[23]>1) REJECT("Cabecalho ESP invalido");
    out->chip_id=le16(header+12);
    if (out->chip_id!=chip) REJECT("Chip incompatível com o destino");
    out->min_revision=le16(header+15);
    out->max_revision=le16(header+17);
    if (revision!=UINT16_MAX && (revision<out->min_revision || (out->max_revision && revision>out->max_revision)))
        REJECT("Revisao do chip fora do intervalo da imagem");
    uint32_t starts[16]={0}, lengths[16]={0};
    uint32_t offset=24;
    uint8_t checksum=0xEF;
    uint8_t buffer[1024];
    for (unsigned i=0;i<header[1];i++) {
        uint8_t segment[8];
        if (offset>size || size-offset<8 || !read(ctx,offset,segment,8)) REJECT("Cabecalho de segmento truncado");
        uint32_t address=le32(segment), length=le32(segment+4);
        offset+=8;
        if ((length & 3) || length>size-offset || address>UINT32_MAX-length) REJECT("Segmento fora dos limites/alinhamento");
        if (address && length) {
            for (unsigned j=0;j<i;j++)
                if (starts[j] && lengths[j] && address<starts[j]+lengths[j] && starts[j]<address+length)
                    REJECT("Segmentos de memoria sobrepostos");
        }
        starts[i]=address; lengths[i]=length;
        if (i==0) {
            uint8_t desc[256];
            if (length<sizeof(desc) || !read(ctx,offset,desc,sizeof(desc)) || le32(desc)!=0xABCD5432)
                REJECT("Sem descricao de aplicativo: bootloader/merged nao e app-only");
            memcpy(out->version,desc+16,32); memcpy(out->project,desc+48,32); memcpy(out->idf,desc+112,32);
            char *fields[]={out->version,out->project,out->idf};
            for (unsigned field=0;field<3;field++)
                for (unsigned k=0;k<32 && fields[field][k];k++)
                    if ((unsigned char)fields[field][k]<32 || (unsigned char)fields[field][k]>126) fields[field][k]='?';
        }
        uint32_t left=length;
        while (left) {
            size_t n=left<sizeof(buffer)?left:sizeof(buffer);
            if (!read(ctx,offset,buffer,n)) REJECT("Erro de leitura no segmento");
            for (size_t k=0;k<n;k++) checksum ^= buffer[k];
            offset+=n; left-=n;
        }
    }
    uint32_t entry=le32(header+4); bool entry_mapped=false;
    for (unsigned i=0;i<header[1];i++)
        if (starts[i] && entry>=starts[i] && entry-starts[i]<lengths[i]) entry_mapped=true;
    if (!entry_mapped || (entry&1)) REJECT("Entrada de execucao fora dos segmentos");
    uint32_t padded=(offset+16)&~15u;
    if (padded>size || !read(ctx,padded-1,buffer,1)) REJECT("Checksum truncado");
    if (buffer[0]!=checksum) REJECT("Checksum invalido");
    uint32_t expected_size=padded+(header[23]?32:0);
    if (size!=expected_size) REJECT("Dados adicionais: assinatura/merged/backup nao suportado");
    if (header[23]) {
        uint8_t digest[32];
        if (!read(ctx,padded,digest,32) || !hash || !hash(ctx,padded,digest)) REJECT("SHA-256 invalido ou nao verificavel");
        out->hash_verified=true;
    }
    out->segments=header[1]; out->image_size=expected_size;
    return true;
}

bool firmware_parse(void *ctx,fw_read_fn read,fw_hash_fn hash,uint32_t size,uint16_t revision,firmware_image_t *out,char *error,size_t n){return firmware_parse_chip(ctx,read,hash,size,FW_P4_CHIP_ID,revision,out,error,n);}
