#include "common.h"
#include <stdlib.h>
#include <stdio.h>

#define HASH_SIZE 1000003
#define BLOCK_SIZE 5000  // Bloques más grandes para reducir accesos a disco

unsigned int hash_function(uint64_t key) {
    return key % HASH_SIZE;
}

void hash_table_insert(HashTable* table, uint64_t key, long offset) {
    unsigned int bucket = hash_function(key);
    HashEntry* entry = malloc(sizeof(HashEntry));
    if (!entry) {
        perror("Error al asignar memoria para HashEntry");
        exit(1);
    }
    entry->key = key;
    entry->offset = offset;
    entry->next = table->buckets[bucket];
    table->buckets[bucket] = entry;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <input_csv>\n", argv[0]);
        return 1;
    }

    FILE *csv = fopen(argv[1], "r");
    if (!csv) {
        perror("Error opening CSV file");
        return 1;
    }

    FILE *data_file = fopen(DATA_FILE, "wb");
    FILE *slot_file = fopen(SLOT_INDEX_FILE, "wb");
    FILE *meta_file = fopen(METADATA_FILE, "wb");
    
    if (!data_file || !slot_file || !meta_file) {
        perror("Error opening output files");
        fclose(csv);
        if (data_file) fclose(data_file);
        if (slot_file) fclose(slot_file);
        if (meta_file) fclose(meta_file);
        return 1;
    }

    HashTable hash_table;
    hash_table.size = HASH_SIZE;
    hash_table.buckets = calloc(HASH_SIZE, sizeof(HashEntry*));
    if (!hash_table.buckets) {
        perror("Error al asignar memoria para la tabla hash");
        fclose(csv);
        fclose(data_file);
        fclose(slot_file);
        fclose(meta_file);
        return 1;
    }

    char line[1024];
    if (!fgets(line, sizeof(line), csv)) {
        perror("Error leyendo encabezado");
        fclose(csv);
        fclose(data_file);
        fclose(slot_file);
        fclose(meta_file);
        free(hash_table.buckets);
        return 1;
    }
    
    Record record;
    BlockIndex block_idx = {0};
    Metadata meta = {
        .record_count = 0,
        .block_count = 0,
        .record_size = sizeof(Record)
    };
    
    long current_offset = 0;
    unsigned int current_block_min = 0;
    unsigned int current_block_max = 0;
    long current_block_offset = 0;
    int records_in_current_block = 0;

    while (fgets(line, sizeof(line), csv)) {
        if (sscanf(line, "%19[^,],%u,%u,%49[^,],%4[^,],%99[^,],%llu,%llu,%llu,%llu,%99[^,],%lu,%lu,%lu,%lu",
               record.block_time, &record.slot, &record.tx_idx, record.signing_wallet, 
               record.direction, record.base_coin, &record.base_coin_amount, 
               &record.quote_coin_amount, &record.virtual_token_balance_after, 
               &record.virtual_sol_balance_after, record.signature, 
               &record.provided_gas_fee, &record.provided_gas_limit, 
               &record.fee, &record.consumed_gas) != 15) {
            fprintf(stderr, "Error parsing line: %s\n", line);
            continue;
        }

        // Escribir registro
        fwrite(&record, sizeof(Record), 1, data_file);
        
        // Insertar en tabla hash
        uint64_t key = ((uint64_t)record.slot << 32) | record.tx_idx;
        hash_table_insert(&hash_table, key, current_offset);
        
        // Manejar índice de bloques
        if (records_in_current_block == 0) {
            // Nuevo bloque
            current_block_min = record.slot;
            current_block_max = record.slot;
            current_block_offset = current_offset;
            records_in_current_block = 1;
        } else {
            // Actualizar bloque actual
            if (record.slot < current_block_min) current_block_min = record.slot;
            if (record.slot > current_block_max) current_block_max = record.slot;
            records_in_current_block++;
            
            // Si el bloque está lleno, escribirlo
            if (records_in_current_block >= BLOCK_SIZE) {
                BlockIndex bi = {
                    .min_slot = current_block_min,
                    .max_slot = current_block_max,
                    .offset = current_block_offset
                };
                fwrite(&bi, sizeof(BlockIndex), 1, slot_file);
                meta.block_count++;
                records_in_current_block = 0;
            }
        }
        
        current_offset += sizeof(Record);
        meta.record_count++;
    }
    
    // Escribir último bloque si existe
    if (records_in_current_block > 0) {
        BlockIndex bi = {
            .min_slot = current_block_min,
            .max_slot = current_block_max,
            .offset = current_block_offset
        };
        fwrite(&bi, sizeof(BlockIndex), 1, slot_file);
        meta.block_count++;
    }
    
    // Escribir metadatos
    fwrite(&meta, sizeof(Metadata), 1, meta_file);
    
    // Guardar tabla hash
    FILE* hash_file = fopen("hashtable.bin", "wb");
    if (!hash_file) {
        perror("Error abriendo archivo de tabla hash");
    } else {
        for (int i = 0; i < HASH_SIZE; i++) {
            HashEntry* entry = hash_table.buckets[i];
            while (entry) {
                fwrite(&entry->key, sizeof(uint64_t), 1, hash_file);
                fwrite(&entry->offset, sizeof(long), 1, hash_file);
                entry = entry->next;
            }
        }
        fclose(hash_file);
    }
    
    // Liberar memoria
    for (int i = 0; i < HASH_SIZE; i++) {
        HashEntry* entry = hash_table.buckets[i];
        while (entry) {
            HashEntry* temp = entry;
            entry = entry->next;
            free(temp);
        }
    }
    free(hash_table.buckets);
    
    // Cerrar archivos
    fclose(csv);
    fclose(data_file);
    fclose(slot_file);
    fclose(meta_file);
    
    printf("Preprocesamiento completado. Registros: %d, Bloques: %d\n", 
           meta.record_count, meta.block_count);
    return 0;
}