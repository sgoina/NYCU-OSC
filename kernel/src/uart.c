#include "uart.h"
#include "deviceTree.h"
#include "ring_buffer.h"

static unsigned long uart_base_addr = 0;

#define UART_RBR  (volatile unsigned char*)(uart_base_addr + 0x0)  // Receive Buffer Register
#define UART_THR  (volatile unsigned char*)(uart_base_addr + 0x0)  // Transmit Holding Register
#define UART_IER  (volatile unsigned char*)(uart_base_addr + 0x4)
#define UART_IIR  (volatile unsigned char*)(uart_base_addr + 0x8)
#define UART_MCR  (volatile unsigned char*)(uart_base_addr + 0x10)
#define UART_LSR  (volatile unsigned char*)(uart_base_addr + 0x14) // Line Status Register

#define LSR_DR    (1 << 0) // Data Ready (In Receive Buffer Register)
#define LSR_TDRQ  (1 << 5) // Transmit Data Request (Transferred from the Transmit Holding Register)

RingBuffer rx_buf = { .head = 0, .tail = 0 };
RingBuffer tx_buf = { .head = 0, .tail = 0 };

void uart_init(unsigned long dtb_ptr) {
    int uart_base_offset = fdt_path_offset(dtb_ptr, "/soc/serial"); // find the offset of "/soc/serial" in the device tree
    if (uart_base_offset == -1)
        return;
    int len = 0;
    const void* prop = fdt_getprop(dtb_ptr, uart_base_offset, "reg", &len); // find the base address of uart
    if (prop != NULL){
        const uint32_t* reg = (const uint32_t*)prop;    
        uint32_t uart_reg = bswap32(reg[1]);
        uart_base_addr = uart_reg;
    }
    else
        return;
    // 開啟接收 (RX) 中斷 (bit 0)
    // 注意：這裡絕對不要開啟 TX 中斷，TX 中斷是由 uart_putc 觸發的
    *UART_IER |= (1 << 0);
    
    // 啟用 UART 的外部中斷訊號路由 (常見於 8250/16550 晶片)
    *UART_MCR |= (1 << 3);
    return;
}

char uart_getc() {
    // 當 RX Buffer 為空時，休眠等待中斷喚醒，取代 busy-waiting
    while (is_empty(&rx_buf)) {
        asm volatile("wfi"); 
    }
    
    // 進入 Critical Section：關閉中斷，保護 shared buffer
    unsigned long sstatus;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus)); 
    
    char c = pop(&rx_buf);
    
    // 離開 Critical Section：恢復中斷狀態
    asm volatile("csrs sstatus, %0" : : "r"(sstatus & 2));
    return c;
}

void uart_putc(char c) {
    // 當 TX Buffer 滿時等待
    if (c == '\n') 
        uart_putc('\r'); 
    // 1. 檢查當前是否處於「關閉中斷」的狀態 (Critical Section)
    unsigned long current_sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(current_sstatus));
    int irq_enabled = current_sstatus & (1 << 1); // 檢查 SIE (bit 1) 是否為 1

    // 2. 當 TX Buffer 滿時的等待邏輯
    while (is_full(&tx_buf)) {
        if (irq_enabled) {
            // 安全：中斷有開，可以安心休眠等硬體叫醒
            asm volatile("wfi");
        } else {
            // 🚨 危險：中斷被關了！絕對不能 wfi！
            // 必須手動擔任 ISR 的角色，把 Buffer 裡的東西丟給硬體
            if (*UART_LSR & LSR_TDRQ) {
                *UART_THR = pop(&tx_buf);
            }
        }
    }

    // 3. 進入 Critical Section 保護 shared buffer (原本的邏輯)
    unsigned long sstatus_temp;
    asm volatile("csrrci %0, sstatus, 2" : "=r"(sstatus_temp));
    
    push(&tx_buf, c);
    
    // 主動開啟 UART 的 TX Empty Interrupt 點火
    *UART_IER |= (1 << 1); 
    
    // 恢復原本的中斷狀態
    asm volatile("csrs sstatus, %0" : : "r"(sstatus_temp & 2));
}

void uart_puts(const char* s) {
    while (*s) uart_putc(*s++);
}

// Print hex number by uart
void uart_hex(unsigned long h) {
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}

// Print decimal number by uart
void uart_dec(unsigned long h) {
    char buf[30];
    int cnt = 0;
    if (h == 0) {
        uart_putc('0');
        return;
    }
    while (h){
        buf[cnt++] = '0' + (h % 10);
        h /= 10;
    }
    for (int i = cnt - 1; i >= 0; i--){
        uart_putc(buf[i]);
    }
}

// ==========================================
// 4. UART 中斷處理器 (在 do_trap 內被呼叫)
// ==========================================
void handle_uart_interrupt() {
    unsigned char lsr = *UART_LSR;

    // (A) 處理接收 (RX) 中斷
    if (lsr & LSR_DR) {
        char c = *UART_RBR; // 讀取硬體暫存器，清除 RX 中斷狀態
        push(&rx_buf, c);
    }

    // (B) 處理發送 (TX) 中斷
    if (lsr & LSR_TDRQ) {
        if (!is_empty(&tx_buf)) {
            // Buffer 內還有資料，丟給硬體送出
            *UART_THR = pop(&tx_buf);
        } else {
            // ⭐【非常重要】：如果 Buffer 已經空了，必須關閉 TX Interrupt
            // 否則硬體會因為一直處於 "Empty" 狀態而無限觸發中斷，導致系統卡死！
            *UART_IER &= ~(1 << 1);
        }
    }
}
