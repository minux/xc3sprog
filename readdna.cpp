/* JTAG chain detection

Copyright (C) 2004 Andrew Rogers

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Changes:
Sandro Amato [sdroamt@netscape.net] 26 Jun 2006 [applied 13 Jul 2006]:
    Print also the location of the devices found in the jtag chain
    to be used from jtagProgram.sh script...
Dmitry Teytelman [dimtey@gmail.com] 14 Jun 2006 [applied 13 Aug 2006]:
    Code cleanup for clean -Wall compile.
    Added support for FT2232 driver.
*/



#include <string.h>
#include <unistd.h>
#include <memory>
#include "io_exception.h"
#include "ioparport.h"
#include "iofx2.h"
#include "ioxpc.h"
#include "ioftdi.h"
#include "iodebug.h"
#include "jtag.h"
#include "devicedb.h"

extern char *optarg;

void usage(void)
{
  fprintf(stderr, 
	  "\nUsage: detectchain [-c cable_type] [-v]\n"
	  "   -v\tverbose output\n\n"
	  "   Supported cable types: pp, ftdi, fx2, xpc\n"
	  "   \tOptional pp arguments:\n"
	  "   \t\t[-d device] (e.g. /dev/parport0)\n"
	  "   \tOptional fx2/ftdi/xpc arguments:\n"
	  "   \t\t[-V vendor]      (idVendor)\n"
	  "   \t\t[-P product]     (idProduct)\n"
	  "   \t\t[-D description] (Product string)\n"
	  "   \t\t[-s serial]      (SerialNumber string)\n"
	  "   \tOptional ftdi arguments:\n"
	  "   \t\t[-t subtype]\n"
	  "   \t\t\t(NONE\t\t(0x0403:0x0610) or\n"
	  "   \t\t\t IKDA\t\t(0x0403:0x0610, EN_N on ACBUS2) or\n"
	  "   \t\t\t OLIMEX\t\t(0x15b1:0x0003, JTAG_EN_N on ADBUS4, LED on ACBUS3))\n"
	  "   \t\t\t AMONTEC\t(0x0403:0xcff8, JTAG_EN_N on ADBUS4)\n"
	  "   \tOptional xpc arguments:\n"
	  "   \t\t[-t subtype] (NONE or INT  (Internal Chain on XPC, doesn't work for now on DLC10))\n");
  exit(255);
}

unsigned int get_id(Jtag &jtag, DeviceDB &db, int chainpos, bool verbose)
{
  int num=jtag.getChain();
  int family, manufacturer;
  unsigned int id;

  if (num == 0)
    {
      fprintf(stderr,"No JTAG Chain found\n");
      return 0;
    }
  // Synchronise database with chain of devices.
  for(int i=0; i<num; i++){
    int length=db.loadDevice(jtag.getDeviceID(i));
    if(length>0)jtag.setDeviceIRLength(i,length);
    else{
      id=jtag.getDeviceID(i);
      fprintf(stderr,"Cannot find device having IDCODE=%08x\n",id);
      return 0;
    }
  }
  
  if(jtag.selectDevice(chainpos)<0){
    fprintf(stderr,"Invalid chain position %d, position must be less than %d (but not less than 0).\n",chainpos,num);
    return 0;
  }

  const char *dd=db.getDeviceDescription(chainpos);
  id = jtag.getDeviceID(chainpos);
  if (verbose)
  {
    printf("JTAG chainpos: %d Device IDCODE = 0x%08x\tDesc: %s\nProgramming: ", chainpos,id, dd);
    fflush(stdout);
  }
  return id;
}
  
int main(int argc, char **args)
{
    bool        verbose = false;
    char const *cable   = "pp";
    char const *dev     = 0;
    int vendor    = 0;
    int product   = 0;
    char const *desc    = 0;
    char const *serial  = 0;
    int subtype = FTDI_NO_EN;
    long value;
    byte idata[8];
    byte odata[8];
    int chainpos =0;
    unsigned int id;
    char *devicedb = NULL;
    DeviceDB db(devicedb);
   
    // Start from parsing command line arguments
    while(true) {
	switch(getopt(argc, args, "?hvc:d:V:P:D:S:t:")) {
	    case -1:
		goto args_done;
		
	    case 'v':
		verbose = true;
		break;
      
	    case 'c':
		cable = optarg;
		break;
		
	    case 'd':
		dev = optarg;
		break;
		
	    case 'V':
	      value = strtol(optarg, NULL, 0);
	      vendor = value;
	      break;
		
	    case 'P':
	      value = strtol(optarg, NULL, 0);
	      product = value;
	      break;
		
	    case 'D':
	      desc = optarg;
	      break;
		
	    case 'S':
		serial = optarg;
		break;
		
	    case 't':
		if (strcasecmp(optarg, "ikda") == 0)
		    subtype = FTDI_IKDA;
		else if (strcasecmp(optarg, "olimex") == 0)
		    subtype = FTDI_OLIMEX;
		else if (strcasecmp(optarg, "amontec") == 0)
		    subtype = FTDI_AMONTEC;
		else if (strcasecmp(optarg, "int") == 0)
		    subtype = XPC_INTERNAL;
		else
		    usage();
		break;
	case '?':
	case 'h':
	default:
	  usage();
	}
    }
args_done:
  // Get rid of options
  //printf("argc: %d\n", argc);
  argc -= optind;
  args += optind;
  //printf("argc: %d\n", argc);
  if(argc != 0)  usage();

  std::auto_ptr<IOBase>  io;
  try {
    if     (strcmp(cable, "pp"  ) == 0)  io.reset(new IOParport(dev));
    else if(strcmp(cable, "ftdi") == 0)  
      {
	if ((subtype == FTDI_NO_EN) || (subtype == FTDI_IKDA))
	  {
	    if (vendor == 0)
	      vendor = VENDOR_FTDI;
	    if(product == 0)
	      product = DEVICE_DEF;
	  }
	else if (subtype ==  FTDI_OLIMEX)
	  {
	    if (vendor == 0)
	      vendor = VENDOR_OLIMEX;
	    if(product == 0)
	      product = DEVICE_OLIMEX_ARM_USB_OCD;
	  }
	else if (subtype ==  FTDI_AMONTEC)
	  {
	    if (vendor == 0)
	      vendor = VENDOR_FTDI;
	    if(product == 0)
	      product = DEVICE_AMONTEC_KEY;
	  }
	io.reset(new IOFtdi(vendor, product, desc, serial, subtype));
      }
    else if(strcmp(cable,  "fx2") == 0)  
      {
	if (vendor == 0)
	  vendor = USRP_VENDOR;
	if(product == 0)
	  product = USRP_DEVICE;
	io.reset(new IOFX2(vendor, product, desc, serial));
      }
    else if(strcmp(cable,  "xpc") == 0)  
      {
	if (vendor == 0)
	  vendor = XPC_VENDOR;
	if(product == 0)
	  product = XPC_DEVICE;
	io.reset(new IOXPC(vendor, product, desc, serial, subtype));
      }
    else  usage();

    io->setVerbose(verbose);
  }
  catch(io_exception& e) 
    {
      if(strcmp(cable, "pp") != 0) 
	{
	  fprintf(stderr, "Could not access USB device %04x:%04x.\n", 
		  vendor, product);
	}
      return 1;
    }

  
  if (verbose)
    fprintf(stderr, "Using %s\n", db.getFile().c_str());
  Jtag jtag(io.get());
  id = get_id (jtag, db, chainpos, verbose);

  int dblast=0;
  if (verbose)
    fprintf(stderr, "Using %s\n", db.getFile().c_str());
#define CFG_IN      0x05
#define ISC_ENABLE  0x10
#define ISC_DISABLE 0x16
#define JPROGRAM    0x0b
#define ISC_DNA     0x31
#define BYPASS      0x3f
  jtag.selectDevice(chainpos);

  idata[0] = JPROGRAM;
  jtag.shiftIR(idata);
  idata[0] = CFG_IN;
  do
    jtag.shiftIR(idata, odata);
  while (! (odata[0] & 0x10)); /* wait until configuration cleared */
  /* As ISC_DNA only works on a unconfigured device, see AR #29977*/

  idata[0] = ISC_ENABLE;
  jtag.shiftIR(idata);
  idata[0] = ISC_DNA;
  jtag.shiftIR(idata);
  jtag.shiftDR(0, odata, 64);
  if (*(long long*)odata != -1LL)
    printf("DNA is 0x%02x%02x%02x%02x%02x%02x%02x%02x\n", 
		 odata[0], odata[1], odata[2], odata[3], 
		 odata[4], odata[5], odata[6], odata[7]);

  idata[0] = ISC_DISABLE;
  jtag.shiftIR(idata);

  /* Release JTAG control over configuration (AR 16829)*/
  io->tapTestLogicReset();
  idata[0] = JPROGRAM;
  jtag.shiftIR(idata); 
  /* Now device will reconfigure from standard configuration source */

  return 0;
}
