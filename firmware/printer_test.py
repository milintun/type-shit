from escpos.printer import Usb
p = Usb(0x0485, 0x5741)
p.text("FART FART FART\nFART FART FART\nFART FART FART\nFART FART FART\n")
p.close()