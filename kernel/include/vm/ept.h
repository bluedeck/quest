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

#ifndef _EPT_H_
#define _EPT_H_

#include <types.h>

#define PHYS_PAGE_SIZE       4096
#define APIC_VIRT_ADDR       0xFEC00000
/* Physical memory offset between each sandbox kernel */
#define SANDBOX_KERN_OFFSET  0x08000000
#define SCREEN_PHYS          0x000B8000
/* Size of EPT paging structure, 10MB for each kernel. */
#define EPT_DATA_SIZE        0x00A00000

extern void vmx_init_mem (uint32);
extern void vmx_init_ept (uint32);

#endif
