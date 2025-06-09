#include "argtable3.h"
#include "gelf.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define ERROR_EXIT(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)

static size_t
gb(size_t x)
{
    return 1024 * 1024 * 1024 * x;
}

static uintptr_t
ceilp(uintptr_t addr, size_t align)
{
    size_t align_mask = align - 1;
    return (addr + align_mask) & ~align_mask;
}

static void
checkelf(Elf64_Ehdr *ehdr)
{
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_type != ET_DYN) {
        fprintf(stderr, "elf check failed\n");
        exit(EXIT_FAILURE);
    }
}

static void
write_file(const char *filename, void *data, size_t size)
{
    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0755);
    if (fd < 0)
        ERROR_EXIT("open output");

    if (write(fd, data, size) != size)
        ERROR_EXIT("write");

    close(fd);
}

static void *
map_file(const char *filename, size_t *filesize)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0)
        ERROR_EXIT("open");

    struct stat st;
    if (fstat(fd, &st) < 0)
        ERROR_EXIT("fstat");

    void *map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED)
        ERROR_EXIT("mmap");

    close(fd);
    *filesize = st.st_size;
    return map;
}

static void
emptyseg(Elf64_Phdr *phdr, Elf64_Word p_type, size_t *p_vaddr, size_t p_filesz, size_t p_memsz, Elf64_Word p_flags, size_t p_align)
{
    phdr->p_type = p_type;
    phdr->p_filesz = p_filesz;
    phdr->p_memsz = p_memsz;
    phdr->p_flags = p_flags;
    phdr->p_align = p_align;
    phdr->p_vaddr = *p_vaddr;
    phdr->p_paddr = *p_vaddr;
    *p_vaddr = *p_vaddr + p_memsz;
}

int
main(int argc, char **argv)
{
    struct arg_lit *help = arg_lit0("h", "help", "Show help");
    struct arg_lit *verbose = arg_lit0("V",  "verbose", "Verbose output");
    struct arg_file *output = arg_filen("o", "output", "<file>", 1, 100, "Output file");
    struct arg_file *inputs = arg_filen(NULL, NULL, "<file>", 2, 100, "Input files");
    struct arg_end *end = arg_end(20);

    void *argtable[] = { help, verbose, output, inputs, end };

    if (arg_nullcheck(argtable) != 0) {
        fprintf(stderr, "Memory allocation error\n");
        return 1;
    }

    int nerrors = arg_parse(argc, argv, argtable);

    if (help->count > 0) {
        printf("Usage: %s", argv[0]);
        arg_print_syntax(stdout, argtable, "\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        return 0;
    }

    if (nerrors > 0) {
        arg_print_errors(stderr, end, argv[0]);
        return 1;
    }

    if (verbose->count > 0) {
        printf("Verbose mode enabled\n");
    }

    assert(inputs->count >= 2);

    const char *sbox = inputs->filename[0];
    const char *host = inputs->filename[1];

    size_t host_size, sbox_size;
    uint8_t *host_map = map_file(host, &host_size);
    uint8_t *sbox_map = map_file(sbox, &sbox_size);

    Elf64_Ehdr *host_ehdr = (Elf64_Ehdr *) host_map;
    Elf64_Ehdr *sbox_ehdr = (Elf64_Ehdr *) sbox_map;

    checkelf(host_ehdr);
    checkelf(sbox_ehdr);

    Elf64_Phdr *host_phdrs = (Elf64_Phdr *) (host_map + host_ehdr->e_phoff);
    Elf64_Phdr *sbox_phdrs = (Elf64_Phdr *) (sbox_map + sbox_ehdr->e_phoff);

    size_t pagesize = 0x1000;
    size_t guardsize = 0x14000;

    // Prepare new memory buffer to hold modified shared object
    size_t out_size = host_size + sbox_size + pagesize;
    uint8_t *out_map = calloc(out_size, 1);
    if (!out_map)
        ERROR_EXIT("malloc");

    // Copy original shared object.
    memcpy(out_map, host_map, host_size);

    Elf64_Phdr *new_phdrs = (Elf64_Phdr *) (out_map + host_ehdr->e_phoff);

    int null_idx = 0;
    size_t host_end_vaddr = 0;
    for (int i = 0; i < host_ehdr->e_phnum; ++i) {
        if (host_phdrs[i].p_type == PT_LOAD) {
            host_end_vaddr = host_phdrs[i].p_vaddr + host_phdrs[i].p_memsz;
        }
        if (host_phdrs[i].p_type == PT_NULL) {
            null_idx = i;
            break;
        }
    }

    size_t sbox_vaddr = ceilp(host_end_vaddr, pagesize);

    int out_idx = null_idx;
    assert(host_ehdr->e_phnum > out_idx);
    emptyseg(&new_phdrs[out_idx++], PT_LOAD, &sbox_vaddr, 0, guardsize, 0, pagesize);
    assert(host_ehdr->e_phnum > out_idx);
    emptyseg(&new_phdrs[out_idx++], PT_LOAD, &sbox_vaddr, 0, pagesize, PF_R | PF_W, pagesize);

    size_t embed_start = ceilp(host_size, pagesize);

    size_t sbox_base = sbox_vaddr;

    size_t data_offset = 0;
    size_t filesz = 0;

    for (int i = 0; i < sbox_ehdr->e_phnum; ++i) {
        if (sbox_phdrs[i].p_type == PT_LOAD) {
            Elf64_Phdr *src = &sbox_phdrs[i];

            // Copy data from static ELF
            data_offset = embed_start + src->p_offset;
            filesz = src->p_filesz;
            memcpy(out_map + data_offset, sbox_map + src->p_offset, src->p_filesz);

            // Replace PT_NULL with new PT_LOAD
            assert(host_ehdr->e_phnum > out_idx);
            assert(new_phdrs[out_idx].p_type == PT_NULL);
            new_phdrs[out_idx] = *src;
            new_phdrs[out_idx].p_offset = data_offset;
            new_phdrs[out_idx].p_vaddr = sbox_base + src->p_vaddr;
            new_phdrs[out_idx].p_paddr = new_phdrs[i].p_vaddr;
            sbox_vaddr = new_phdrs[out_idx].p_vaddr + new_phdrs[out_idx].p_memsz;
            out_idx++;
        }
    }

    assert(host_ehdr->e_phnum > out_idx);
    sbox_vaddr = ceilp(sbox_vaddr, pagesize);
    emptyseg(&new_phdrs[out_idx], PT_LOAD, &sbox_vaddr, 0, gb(4) - (sbox_vaddr - sbox_base) + guardsize, 0, pagesize);

    // Write modified ELF to new file
    write_file(output->filename[0], out_map, data_offset + filesz);

    printf("Modified ELF written to %s\n", output->filename[0]);

    free(out_map);
    munmap(host_map, host_size);
    munmap(sbox_map, sbox_size);

    return 0;
}
