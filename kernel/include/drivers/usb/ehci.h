/*                    The Quest Operating System
 *  Copyright (C) 2005-2010  Richard West, Boston University
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef _EHCI_H_
#define _EHCI_H_


#include <types.h>
#include <arch/i386.h>
#include <drivers/pci/pci.h>
#include <drivers/usb/usb.h>
#include <util/list.h>
#include <util/cassert.h>


#define hcd_to_ehci_hcd(hcd) container_of((hcd), ehci_hcd_t, usb_hcd)
#define ehci_hcd_to_hcd(ehci_hcd) (&((ehci_hcd)->usb_hcd))


/*
 * These constants affect system performance, would be worth playing
 * around with these numbers are some point to see how much they
 * affect overall performance
 */


/* Number of qtd retries Possible values: 0-3   0 -> don't stop */
#define EHCI_TUNE_CERR 3

/* nak throttle, see section 4.9 of EHCI specs */
#define EHCI_TUNE_RL_HS 4

/* 1-3 transactions per microframe see section 4.10.3 of EHCI specs */
#define EHCI_TUNE_MULT_HS 1

#define EHCI_TUNE_MULT_TT 1

typedef uint32_t ehci_port_t;



#define TYPE_ITD  0
#define TYPE_QH   1
#define TYPE_SITD 2
#define TYPE_FSTN 3


typedef struct
{
  uint8_t  cap_length;
  uint8_t  reserved;
  uint16_t hci_version;
  uint32_t hcs_params;

#define N_PORTS (0xF<<0)

#define GET_NUM_PORTS(hcd) ((hcd)->caps->hcs_params & N_PORTS)
  
  uint32_t hcc_params;

#define PROG_FRM_LST_LENGTH (1<<1)
  
#define EHCI_IS_FRM_LST_LEN_PROG(hcd)           \
  ((hcd)->caps->hcc_params & PROG_FRM_LST_LENGTH)
  
  uint8_t port_route[8];
} ehci_caps_t PACKED;

typedef struct
{
  uint32_t command;

#define RUN (1<<0)
#define RESET (1<<1)
#define PERIODIC_SCHED_ENABLE (1<<4)
#define ASYNC_SCHED_ENABLE (1<<5)
#define INT_ASYNC_ADV_DOORBELL (1<<6)
#define INT_THRESHOLD (0xFF<<16)


#define EHCI_ENABLE_ASYNC(hcd) ((hcd)->regs->command |= ASYNC_SCHED_ENABLE)
#define EHCI_ENABLE_PERIODIC(hcd) ((hcd)->regs->command |= PERIODIC_SCHED_ENABLE)
#define EHCI_IS_RUNNING(hcd) ((hcd)->regs->command & RUN)
#define EHCI_RUN(hcd) (hcd)->regs->command |= RUN
#define EHCI_HALT(hcd) (hcd)->regs->command &= ~RUN
#define EHCI_RESET(hcd) (hcd)->regs->command |= RESET
#define EHCI_ASYNC_DOORBELL_ENABLED(hcd) ((hcd)->regs->command | INT_ASYNC_ADV_DOORBELL)
#define EHCI_ENABLE_ASYNC_DOORBELL(hcd) ((hcd)->regs->command |= INT_ASYNC_ADV_DOORBELL)
#define EHCI_DISABLE_ASYNC_DOORBELL(hcd) ((hcd)->regs->command &= ~INT_ASYNC_ADV_DOORBELL)
  
#define EHCI_SET_FRAME_LIST_SIZE(hcd, size)                       \
  do {                                                            \
    uint32_t __command_temp = (hcd)->regs->command;               \
    __command_temp &= ~(3<<2);                                    \
    (hcd)->regs->command = __command_temp | ((size)<<2);          \
  } while(0)
  
#define EHCI_SET_INT_THRESHOLD(hcd, threshold)                    \
  do {                                                            \
    uint32_t __command_temp = (hcd)->regs->command;               \
    __command_temp &= ~INT_THRESHOLD;                             \
    (hcd)->regs->command = __command_temp | ((threshold) << 16);  \
  } while(0)
  
  uint32_t status;
  
#define HALTED (1<<12)
#define EHCI_IS_HALTED(hcd) ((hcd)->regs->status & HALTED)
#define EHCI_ACK_INTERRUPTS(hcd) ((hcd)->regs->status |= USBINTR_MASK)
#define EHCI_ASYNC_DOORBELL_RUNG(hcd) ((hcd)->regs->status & USBINTR_IAA)
#define EHCI_ACK_DOORBELL(hcd) ((hcd)->regs->status |= USBINTR_IAA)
  
  
  
  uint32_t interrupt_enable;

#define USBINTR_IAA  (1<<5)          /* Interrupted on async advance       */
#define USBINTR_HSE  (1<<4)          /* such as some PCI access errors     */
#define USBINTR_FLR  (1<<3)          /* frame list rolled over             */
#define USBINTR_PCD  (1<<2)          /* port change detect                 */
#define USBINTR_ERR  (1<<1)          /* "error" completion (overflow, ...) */
#define USBINTR_INT  (1<<0)          /* "normal" completion (short, ...)   */
#define USBINTR_MASK (USBINTR_IAA | USBINTR_HSE | USBINTR_PCD | USBINTR_ERR | USBINTR_INT )

#define EHCI_ENABLE_INTR(hcd, intr) ((hcd)->regs->interrupt_enable |= (intr))
#define EHCI_SET_INTRS(hcd) EHCI_ENABLE_INTR((hcd), USBINTR_MASK)
#define EHCI_DISABLE_INTR(hcd, intr) ((hcd)->regs->interrupt_enable &= ~(intr))
#define EHCI_INTR_ENABLED(hcd, intr) ((hcd)->regs->interrupt_enable & (intr))
  
  uint32_t frame_index;

  /*
   * -- EM -- Right now being lazy and making an extremely safe
   * (adding 40 (5 frames)) estimate of the safest frame index.  Fix
   * this later to appropriately check how much the EHCI chip caches
   * (or perhaps not since we will have a real-time system this should
   * all be periodic this macro might never be used in the RT system)
   */
  /*
   * Should only call this macro right before adding itds to periodic
   * list, the value returned should be checked to see if it goes
   * beyond the frame list size
   */
#define EHCI_GET_SAFE_FRAME_INDEX(hcd) (((hcd)->regs->frame_index  + 40) >> 3)
  
  uint32_t segment;
  uint32_t frame_list;
  uint32_t async_next;

  
  uint32_t reserved[9];
  uint32_t configured_flag;

#define  EHCI_SET_CONFIG_FLAG(hcd, flag)        \
  (hcd)->regs->configured_flag = 0x1 & flag
  
  ehci_port_t port_status[0];

#define CURR_CONN_STATUS (1<<0)
#define CONN_STATUS_CHANGE (1<<1)
#define PORT_ENABLED (1<<2)
#define PORT_ENABLE_CHANGE (1<<3)
#define OVER_CURRENT_ACTIVE (1<<4)
#define OVER_CURRENT_CHANGE (1<<5)
#define FORCE_PORT_RESUME (1<<6)
#define SUSPEND (1<<7)
#define PORT_RESET (1<<8)
#define LINE_STATUS (3<<10)
#define PORT_POWER (1<<12)
#define PORT_OWNER (1<<13)
#define PORT_INDIC_CONTROL (3<<14)
#define PORT_TEST (0xF<<16)
#define WAKE_ON_CON_ENABLED (1<<20)
#define WAKE_ON_DISCON_ENABLED (1<<21)
#define WAKE_ON_OVER_CURR_ENABLED (1<<22)

#define IS_DEVICE_PRESENT(port) (port & CURR_CONN_STATUS)
#define IS_PORT_POWERED(port) (port & PORT_POWER)
#define POWER_PORT(port) port |= PORT_POWER
#define IS_PORT_ENABLED(port) (port & PORT_ENABLED)
#define IS_LOW_SPEED_DEVICE_PRESENT(port) ((port & LINE_STATUS) == (1<<10))
#define RESET_PORT(port)                        \
  do {                                          \
    ehci_port_t __port = port;                  \
    __port |= PORT_RESET;                       \
    __port &= ~PORT_ENABLED;                    \
    port = __port;                              \
  } while(0)

#define PORT_LEAVE_RESET(port) port &= ~PORT_RESET
  
} PACKED ehci_regs_t;



typedef union {
  uint32_t raw;
  struct
  {
    uint32_t tBit:1;
    uint32_t type:2;
    uint32_t reserved:2;
    uint32_t pointer:27;
  } PACKED;
} PACKED frm_lst_lnk_ptr_t;


#define EHCI_LIST_END 1 

#define CLEAR_FRAME_LIST_LINK_POINTER(frame_pointer)    \
  (frame_pointer.raw = EHCI_LIST_END)



typedef union {
  uint32_t raw;
  struct {
    uint32_t buffer_point:20;
    uint32_t current_offset:12;  /*
                                  * Also reserved in qtd buffer page
                                  * pointer that do not use current
                                  * offset
                                  */
  } PACKED;
} qtd_buffer_page_pointer_t;

typedef struct _qtd_t
{
  // dword 1, next qTD
  union {
    uint32_t next_pointer_raw;
    struct {
      uint32_t terminate:1;
      uint32_t reserved1:4;
      uint32_t next_qtd_pointer:27;
    } PACKED;
  };


  // dword 2, alternative next qTD
  union {
    uint32_t alt_pointer_raw; 
    struct {
      uint32_t alternative_terminate:1;
      uint32_t reserved2:4;
      uint32_t alternate_next_qtd_pointer:27;
    } PACKED;
  };

  // dword 3, qTD token
  union {
    uint32_t token;
    struct {
      uint32_t status:8;

#define QTD_ACTIVE (1 << 7)
#define QTD_HALT (1 << 6)
#define QTD_STS_PING (1 << 0)
      
      uint32_t pid_code:2;

#define QTD_SETUP (2 << 8)
#define QTD_INPUT (1 << 8)
#define QTD_IS_OUT(qtd) (!((qtd)->token & 0x300))
#define QTD_IS_INPUT(qtd) ((qtd)->token & 0x100)
      
      uint32_t error_counter:2;

#define QTD_CERR 10
      
      uint32_t curr_page:3;
      uint32_t ioc:1;

#define QTD_IOC (1 << 15)

#define QTD_ENABLE_IOC(qtd) (qtd->token |= QTD_IOC);
      
      uint32_t total_bytes_to_transfer:15;
      uint32_t data_toggle:1;

#define QTD_TOGGLE (1 << 31)
      
    } PACKED;
  };

  // dword 4-8, buffer page 0-4 ,current offset is used only in buffer page 0

  qtd_buffer_page_pointer_t buffer_page[5];
  uint32_t ex_buf_ptr_pgs[5];

  /* Software only members */

  list_head_t chain_list;
  
} PACKED ALIGNED(32) qtd_t;


CASSERT( (sizeof(qtd_t) % 32) == 0, ehci_qtd_size);

#define QH_NEXT(hcd, qh_virt) ( (EHCI_QH_VIRT_TO_PHYS(hcd, qh_virt)) | (TYPE_QH << 1) )

typedef struct _qh_t{
  
  // dword 0
  frm_lst_lnk_ptr_t horizontalPointer;

  // dword 1
  union {
    uint32_t hw_info1;
    struct {
      uint32_t device_address:7;
      uint32_t inactive_on_next:1;
      uint32_t endpoint_num:4;

#define QH_GET_ENDPOINT(qh) (((qh)->hw_info1 >> 8) & 0x0F)
      
      uint32_t endpoint_speed:2;
      uint32_t data_toggle_control:1;
      uint32_t head_reclam:1;
      uint32_t max_packet_length:11;
      uint32_t cnt_endpoint:1;
      uint32_t nak_cnt_reload:4;
    } PACKED;
  };

#define QH_DATA_TOGGLE_CONTROL (1 << 14)
#define QH_HEAD (0x00008000)

  // dword 2
  union {
    uint32_t hw_info2;;
    struct
    {
      uint32_t interrupt_sched_mask:8;
      uint32_t split_comp_mask:8;
      uint32_t hub_addr:7;
      uint32_t port_num:7;
      uint32_t mult:2;
    } PACKED;
  };

  // dword 3
  union {
    uint32_t current_qtd_ptr_raw;
    struct {
      uint32_t reserved0:5;
      uint32_t current_qtd_ptr:27;
    } PACKED;
  };

  // dword 4
  union {
    uint32_t next_qtd_ptr_raw;
    struct {
      uint32_t next_tbit:1;
      uint32_t reserved1:4;
      uint32_t next_qtd_ptr:27;
    } PACKED;
  };

  // dword 5
  union {
    uint32_t alt_qtd_ptr_raw;
    struct {
      uint32_t alt_tbit:1;
      uint32_t nak_cnt:4;
      uint32_t alt_next_qtd_ptr:27;
    } PACKED;
  };

  // dword 6
  union {
    uint32_t qtd_token_raw;
    struct {
      uint32_t status:8;
      uint32_t pid_code:2;
      uint32_t cerr:2;
      uint32_t c_page:3;
      uint32_t ioc:1; /* interrupt on complete */
      uint32_t total_bytes_transfer:15;
      uint32_t data_toggle:1;
    } PACKED;
  };

  // dword 7
  union {
    uint32_t raw5;
    struct {
      uint32_t current_offset:12; 
      uint32_t buff_ptr0:20;
    } PACKED;
  };

  // dword 8
  union {
    uint32_t raw6;
    struct {
      uint32_t c_prog_mask:16;
      uint32_t reserved2:4;
      uint32_t buff_ptr1:20;
    } PACKED;
  };

  // dword 9
  union {
    uint32_t raw7;
    struct {
      uint32_t frame_tag:5;
      uint32_t s_bytes:7;
      uint32_t buff_ptr2:20;
    } PACKED;
  };

  // dword 10
  union {
    uint32_t raw8;
    struct {
      uint32_t reserved3:12; 
      uint32_t buff_ptr3:20;
    } PACKED;
  };

  // dword 11
  union {
    uint32_t raw9;
    struct {
      uint32_t reserved4:12; 
      uint32_t buff_ptr4:20;
    } PACKED;
  };

  uint32_t ex_buf_ptr_pgs[5];

  /*
   * Everything after this is used by software only and is not
   * specified by EHCI
   */

  qtd_t* dummy_qtd;
  uint32_t state;
  list_head_t qtd_list;
  list_head_t reclaim_chain;
  USB_DEVICE_INFO* dev;


#define QH_STATE_NOT_LINKED 1 /* Not in the buffer at all */
#define QH_STATE_LINKED     2 /* In the async circular list */
#define QH_STATE_RECLAIM    3 /* In the reclamation list, which means it
                                 is being removed from the circular
                                 buffer but the HC might still have
                                 links to it */

  
} PACKED ALIGNED(32) qh_t;

typedef struct {
  union {
    uint32_t raw;
    struct {
      uint32_t offset:12;
      uint32_t page_selector:3;
      uint32_t ioc:1;
      uint32_t length:12;
      uint32_t status:4;
    } PACKED;
  };
} PACKED itd_transaction_t;

#define ITD_NEXT(hcd, itd_virt)                                 \
  ( (EHCI_ITD_VIRT_TO_PHYS(hcd, itd_virt) ) | (TYPE_ITD << 1) )

CASSERT( (sizeof(qh_t) % 32) == 0, ehci_qh_size);

typedef struct _itd_t {
  frm_lst_lnk_ptr_t next_link_pointer;
  itd_transaction_t transaction[8];

#define ITD_ACTIVE  (1 << 31)
#define ITD_BUF_ERR (1 << 30)
#define ITD_BAB_ERR (1 << 29)
#define ITD_TRN_ERR (1 << 28)
  
  uint32_t buf_ptr[7];

#define ITD_INPUT (1 << 11)

#define ITD_SET_BUF_PTR(itd, page_num, phys_addr)               \
  ( (itd)->buf_ptr[(page_num)] |= ( (phys_addr) & ~0x0FFF ) )
  
  uint32_t ex_buf_ptr_pgs[7];

  /*
   * Everything after this is used by software only and is not
   * specified by EHCI
   */

  list_head_t chain_list;
  uint32_t total_bytes_to_transfer;
} PACKED ALIGNED(32) itd_t;

CASSERT( (sizeof(itd_t) % 32) == 0, ehci_itd_size);


/*
 * -- EM -- There are better ways to store queue heads for endpoints
 * but for now this is good enough, right now 128 instances of this
 * struct takes 4 pages, running even lower on kernel memory
 */ 
typedef struct
{
  /* First Index: Out = 0, In = 1, Second Index: Endpoint # */
  qh_t* queue_heads[2][16];
} ehci_dev_info_t;

typedef struct
{
  usb_hcd_t usb_hcd;

  ehci_dev_info_t ehci_devinfo[USB_MAX_DEVICES+1];
  
  uint32_t bus;
  uint32_t dev;
  uint32_t func;
  uint32_t irq_line;
  uint8_t  handler_vector;
  
  phys_addr_t   base_physical_address;
  addr_t        base_virtual_address;
  ehci_caps_t*  caps;
  ehci_regs_t*  regs;

  frm_lst_lnk_ptr_t* frame_list;
  uint32_t           frame_list_size;

  qh_t*       qh_pool;
  phys_addr_t qh_pool_phys_addr;
  uint32_t    qh_pool_size;
  uint32_t*   used_qh_bitmap;
  uint32_t    used_qh_bitmap_size;
  
  qtd_t*      qtd_pool;
  phys_addr_t qtd_pool_phys_addr;
  uint32_t    qtd_pool_size;
  uint32_t*   used_qtd_bitmap;
  uint32_t    used_qtd_bitmap_size;
  
  itd_t*      itd_pool;
  phys_addr_t itd_pool_phys_addr;
  uint32_t    itd_pool_size;
  uint32_t*   used_itd_bitmap;
  uint32_t    used_itd_bitmap_size;
  
  uint32_t interrupt_threshold;
  uint32_t num_ports;
  
  bool periodic_list_on;

  qh_t* async_head;
  list_head_t reclaim_list;
  
} ehci_hcd_t;

#define EHCI_GET_DEVICE_QH(ehci_hcd, addr, is_input, endpoint)          \
  ((ehci_hcd)->ehci_devinfo[(addr)]                                     \
   .queue_heads[(is_input) && ((endpoint) != 0)][(endpoint)])

#define EHCI_SET_DEVICE_QH(ehci_hcd, addr, is_input, endpoint, qh)      \
  ((ehci_hcd)->ehci_devinfo[(addr)]                                     \
   .queue_heads[(is_input) && ((endpoint) != 0)][(endpoint)] = qh)


int
ehci_control_transfer(ehci_hcd_t* ehci_hcd, 
                      uint8_t address,
                      addr_t setup_req,    /* Use virtual address here */
                      uint32_t setup_len,
                      addr_t setup_data,   /* Use virtual address here */
                      uint32_t data_len,
                      uint32_t packet_len);

int
ehci_bulk_transfer(ehci_hcd_t* ehci_hcd,
                   uint8_t address,
                   uint8_t endpoint,
                   addr_t data,
                   int data_len,
                   int packet_len,
                   uint8_t direction,
                   uint32_t *act_len);

int ehci_isochronous_transfer(ehci_hcd_t* ehci_hcd, uint8_t address,
                              uint8_t endpoint, addr_t data,
                              int data_len, int packet_len,
                              uint8_t direction, uint32_t *act_len);


#define calc_bitmap_size(hcd, pool_size)                        \
  ((hcd)->pool_size + (EHCI_ELEMENTS_PER_BITMAP_ENTRY - 1))     \
  / EHCI_ELEMENTS_PER_BITMAP_ENTRY;

#define calc_used_qh_bitmap_size(hcd)           \
  calc_bitmap_size(hcd, qh_pool_size)

#define calc_used_qtd_bitmap_size(hcd)          \
  calc_bitmap_size(hcd, qtd_pool_size)

#define calc_used_itd_bitmap_size(hcd)          \
  calc_bitmap_size(hcd, itd_pool_size)
#endif // _EHCI_H_

/*
 * Local Variables:
 * indent-tabs-mode: nil
 * mode: C
 * c-file-style: "gnu"
 * c-basic-offset: 2
 * End:
 */

/* vi: set et sw=2 sts=2: */
