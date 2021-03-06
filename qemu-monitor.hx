HXCOMM Use DEFHEADING() to define headings in both help text and texi
HXCOMM Text between STEXI and ETEXI are copied to texi version and
HXCOMM discarded from C version
HXCOMM DEF(command, args, callback, arg_string, help) is used to construct
HXCOMM monitor commands
HXCOMM HXCOMM can be used for comments, discarded from both texi and C

STEXI
@table @option
ETEXI

    { "help|?", "name:s?", do_help_cmd, "[cmd]", "show the help" },
STEXI
@item help or ? [@var{cmd}]
Show the help for all commands or just for command @var{cmd}.
ETEXI

    { "commit", "device:s", do_commit,
      "device|all", "commit changes to the disk images (if -snapshot is used) or backing files" },
STEXI
@item commit
Commit changes to the disk images (if -snapshot is used) or backing files.
ETEXI

    { "info", "item:s?", do_info,
      "[subcommand]", "show various information about the system state" },
STEXI
@item info @var{subcommand}
Show various information about the system state.

@table @option
@item info version
show the version of QEMU
@item info network
show the various VLANs and the associated devices
@item info chardev
show the character devices
@item info block
show the block devices
@item info block
show block device statistics
@item info registers
show the cpu registers
@item info cpus
show infos for each CPU
@item info history
show the command line history
@item info irq
show the interrupts statistics (if available)
@item info pic
show i8259 (PIC) state
@item info pci
show emulated PCI device info
@item info tlb
show virtual to physical memory mappings (i386 only)
@item info mem
show the active virtual memory mappings (i386 only)
@item info hpet
show state of HPET (i386 only)
@item info kvm
show KVM information
@item info usb
show USB devices plugged on the virtual USB hub
@item info usbhost
show all USB host devices
@item info profile
show profiling information
@item info capture
show information about active capturing
@item info snapshots
show list of VM snapshots
@item info status
show the current VM status (running|paused)
@item info pcmcia
show guest PCMCIA status
@item info mice
show which guest mouse is receiving events
@item info vnc
show the vnc server status
@item info name
show the current VM name
@item info uuid
show the current VM UUID
@item info cpustats
show CPU statistics
@item info usernet
show user network stack connection states
@item info migrate
show migration status
@item info balloon
show balloon information
@item info qtree
show device tree
@end table
ETEXI

    { "q|quit", "", do_quit,
      "", "quit the emulator" },
STEXI
@item q or quit
Quit the emulator.
ETEXI

    { "eject", "force:-f,filename:B", do_eject,
      "[-f] device", "eject a removable medium (use -f to force it)" },
STEXI
@item eject [-f] @var{device}
Eject a removable medium (use -f to force it).
ETEXI

    { "change", "device:B,target:F,arg:s?", do_change,
      "device filename [format]", "change a removable medium, optional format" },
STEXI
@item change @var{device} @var{setting}

Change the configuration of a device.

@table @option
@item change @var{diskdevice} @var{filename} [@var{format}]
Change the medium for a removable disk device to point to @var{filename}. eg

@example
(qemu) change ide1-cd0 /path/to/some.iso
@end example

@var{format} is optional.

@item change vnc @var{display},@var{options}
Change the configuration of the VNC server. The valid syntax for @var{display}
and @var{options} are described at @ref{sec_invocation}. eg

@example
(qemu) change vnc localhost:1
@end example

@item change vnc password [@var{password}]

Change the password associated with the VNC server. If the new password is not
supplied, the monitor will prompt for it to be entered. VNC passwords are only
significant up to 8 letters. eg

@example
(qemu) change vnc password
Password: ********
@end example

@end table
ETEXI

    { "screendump", "filename:F", do_screen_dump,
      "filename", "save screen into PPM image 'filename'" },
STEXI
@item screendump @var{filename}
Save screen into PPM image @var{filename}.
ETEXI

    { "logfile", "filename:F", do_logfile,
      "filename", "output logs to 'filename'" },
STEXI
@item logfile @var{filename}
Output logs to @var{filename}.
ETEXI

    { "log", "items:s", do_log,
      "item1[,...]", "activate logging of the specified items to '/tmp/qemu.log'" },
STEXI
@item log @var{item1}[,...]
Activate logging of the specified items to @file{/tmp/qemu.log}.
ETEXI

    { "savevm", "name:s?", do_savevm,
      "[tag|id]", "save a VM snapshot. If no tag or id are provided, a new snapshot is created" },
STEXI
@item savevm [@var{tag}|@var{id}]
Create a snapshot of the whole virtual machine. If @var{tag} is
provided, it is used as human readable identifier. If there is already
a snapshot with the same tag or ID, it is replaced. More info at
@ref{vm_snapshots}.
ETEXI

    { "loadvm", "name:s", do_loadvm,
      "tag|id", "restore a VM snapshot from its tag or id" },
STEXI
@item loadvm @var{tag}|@var{id}
Set the whole virtual machine to the snapshot identified by the tag
@var{tag} or the unique snapshot ID @var{id}.
ETEXI

    { "delvm", "name:s", do_delvm,
      "tag|id", "delete a VM snapshot from its tag or id" },
STEXI
@item delvm @var{tag}|@var{id}
Delete the snapshot identified by @var{tag} or @var{id}.
ETEXI

    { "singlestep", "option:s?", do_singlestep,
      "[on|off]", "run emulation in singlestep mode or switch to normal mode", },
STEXI
@item singlestep [off]
Run the emulation in single step mode.
If called with option off, the emulation returns to normal mode.
ETEXI

    { "stop", "", do_stop,
      "", "stop emulation", },
STEXI
@item stop
Stop emulation.
ETEXI

    { "c|cont", "", do_cont,
      "", "resume emulation", },
STEXI
@item c or cont
Resume emulation.
ETEXI

    { "gdbserver", "device:s?", do_gdbserver,
      "[device]", "start gdbserver on given device (default 'tcp::1234'), stop with 'none'", },
STEXI
@item gdbserver [@var{port}]
Start gdbserver session (default @var{port}=1234)
ETEXI

    { "x", "fmt:/,addr:l", do_memory_dump,
      "/fmt addr", "virtual memory dump starting at 'addr'", },
STEXI
@item x/fmt @var{addr}
Virtual memory dump starting at @var{addr}.
ETEXI

    { "xp", "fmt:/,addr:l", do_physical_memory_dump,
      "/fmt addr", "physical memory dump starting at 'addr'", },
STEXI
@item xp /@var{fmt} @var{addr}
Physical memory dump starting at @var{addr}.

@var{fmt} is a format which tells the command how to format the
data. Its syntax is: @option{/@{count@}@{format@}@{size@}}

@table @var
@item count
is the number of items to be dumped.

@item format
can be x (hex), d (signed decimal), u (unsigned decimal), o (octal),
c (char) or i (asm instruction).

@item size
can be b (8 bits), h (16 bits), w (32 bits) or g (64 bits). On x86,
@code{h} or @code{w} can be specified with the @code{i} format to
respectively select 16 or 32 bit code instruction size.

@end table

Examples:
@itemize
@item
Dump 10 instructions at the current instruction pointer:
@example
(qemu) x/10i $eip
0x90107063:  ret
0x90107064:  sti
0x90107065:  lea    0x0(%esi,1),%esi
0x90107069:  lea    0x0(%edi,1),%edi
0x90107070:  ret
0x90107071:  jmp    0x90107080
0x90107073:  nop
0x90107074:  nop
0x90107075:  nop
0x90107076:  nop
@end example

@item
Dump 80 16 bit values at the start of the video memory.
@smallexample
(qemu) xp/80hx 0xb8000
0x000b8000: 0x0b50 0x0b6c 0x0b65 0x0b78 0x0b38 0x0b36 0x0b2f 0x0b42
0x000b8010: 0x0b6f 0x0b63 0x0b68 0x0b73 0x0b20 0x0b56 0x0b47 0x0b41
0x000b8020: 0x0b42 0x0b69 0x0b6f 0x0b73 0x0b20 0x0b63 0x0b75 0x0b72
0x000b8030: 0x0b72 0x0b65 0x0b6e 0x0b74 0x0b2d 0x0b63 0x0b76 0x0b73
0x000b8040: 0x0b20 0x0b30 0x0b35 0x0b20 0x0b4e 0x0b6f 0x0b76 0x0b20
0x000b8050: 0x0b32 0x0b30 0x0b30 0x0b33 0x0720 0x0720 0x0720 0x0720
0x000b8060: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
0x000b8070: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
0x000b8080: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
0x000b8090: 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720 0x0720
@end smallexample
@end itemize
ETEXI

    { "p|print", "fmt:/,val:l", do_print,
      "/fmt expr", "print expression value (use $reg for CPU register access)", },
STEXI
@item p or print/@var{fmt} @var{expr}

Print expression value. Only the @var{format} part of @var{fmt} is
used.
ETEXI

    { "i", "fmt:/,addr:i,index:i.", do_ioport_read,
      "/fmt addr", "I/O port read" },
STEXI
Read I/O port.
ETEXI

    { "o", "fmt:/,addr:i,val:i", do_ioport_write,
      "/fmt addr value", "I/O port write" },
STEXI
Write to I/O port.
ETEXI

    { "sendkey", "string:s,hold_time:i?", do_sendkey,
      "keys [hold_ms]", "send keys to the VM (e.g. 'sendkey ctrl-alt-f1', default hold time=100 ms)" },
STEXI
@item sendkey @var{keys}

Send @var{keys} to the emulator. @var{keys} could be the name of the
key or @code{#} followed by the raw value in either decimal or hexadecimal
format. Use @code{-} to press several keys simultaneously. Example:
@example
sendkey ctrl-alt-f1
@end example

This command is useful to send keys that your graphical user interface
intercepts at low level, such as @code{ctrl-alt-f1} in X Window.
ETEXI

    { "system_reset", "", do_system_reset,
      "", "reset the system" },
STEXI
@item system_reset

Reset the system.
ETEXI

    { "system_powerdown", "", do_system_powerdown,
      "", "send system power down event" },
STEXI
@item system_powerdown

Power down the system (if supported).
ETEXI

    { "sum", "start:i,size:i", do_sum,
      "addr size", "compute the checksum of a memory region" },
STEXI
@item sum @var{addr} @var{size}

Compute the checksum of a memory region.
ETEXI

    { "usb_add", "devname:s", do_usb_add,
      "device", "add USB device (e.g. 'host:bus.addr' or 'host:vendor_id:product_id')" },
STEXI
@item usb_add @var{devname}

Add the USB device @var{devname}.  For details of available devices see
@ref{usb_devices}
ETEXI

    { "usb_del", "devname:s", do_usb_del,
      "device", "remove USB device 'bus.addr'" },
STEXI
@item usb_del @var{devname}

Remove the USB device @var{devname} from the QEMU virtual USB
hub. @var{devname} has the syntax @code{bus.addr}. Use the monitor
command @code{info usb} to see the devices you can remove.
ETEXI

    { "cpu", "index:i", do_cpu_set,
      "index", "set the default CPU" },
STEXI
Set the default CPU.
ETEXI

    { "mouse_move", "dx_str:s,dy_str:s,dz_str:s?", do_mouse_move,
      "dx dy [dz]", "send mouse move events" },
STEXI
@item mouse_move @var{dx} @var{dy} [@var{dz}]
Move the active mouse to the specified coordinates @var{dx} @var{dy}
with optional scroll axis @var{dz}.
ETEXI

    { "mouse_button", "button_state:i", do_mouse_button,
      "state", "change mouse button state (1=L, 2=M, 4=R)" },
STEXI
@item mouse_button @var{val}
Change the active mouse button state @var{val} (1=L, 2=M, 4=R).
ETEXI

    { "mouse_set", "index:i", do_mouse_set,
      "index", "set which mouse device receives events" },
STEXI
@item mouse_set @var{index}
Set which mouse device receives events at given @var{index}, index
can be obtained with
@example
info mice
@end example
ETEXI

#ifdef HAS_AUDIO
    { "wavcapture", "path:s,freq:i?,bits:i?,nchannels:i?", do_wav_capture,
      "path [frequency [bits [channels]]]",
      "capture audio to a wave file (default frequency=44100 bits=16 channels=2)" },
#endif
STEXI
@item wavcapture @var{filename} [@var{frequency} [@var{bits} [@var{channels}]]]
Capture audio into @var{filename}. Using sample rate @var{frequency}
bits per sample @var{bits} and number of channels @var{channels}.

Defaults:
@itemize @minus
@item Sample rate = 44100 Hz - CD quality
@item Bits = 16
@item Number of channels = 2 - Stereo
@end itemize
ETEXI

#ifdef HAS_AUDIO
    { "stopcapture", "n:i", do_stop_capture,
      "capture index", "stop capture" },
#endif
STEXI
@item stopcapture @var{index}
Stop capture with a given @var{index}, index can be obtained with
@example
info capture
@end example
ETEXI

    { "memsave", "val:l,size:i,filename:s", do_memory_save,
      "addr size file", "save to disk virtual memory dump starting at 'addr' of size 'size'", },
STEXI
@item memsave @var{addr} @var{size} @var{file}
save to disk virtual memory dump starting at @var{addr} of size @var{size}.
ETEXI

    { "pmemsave", "val:l,size:i,filename:s", do_physical_memory_save,
      "addr size file", "save to disk physical memory dump starting at 'addr' of size 'size'", },
STEXI
@item pmemsave @var{addr} @var{size} @var{file}
save to disk physical memory dump starting at @var{addr} of size @var{size}.
ETEXI

    { "boot_set", "bootdevice:s", do_boot_set,
      "bootdevice", "define new values for the boot device list" },
STEXI
@item boot_set @var{bootdevicelist}

Define new values for the boot device list. Those values will override
the values specified on the command line through the @code{-boot} option.

The values that can be specified here depend on the machine type, but are
the same that can be specified in the @code{-boot} command line option.
ETEXI

#if defined(TARGET_I386)
    { "nmi", "cpu_index:i", do_inject_nmi,
      "cpu", "inject an NMI on the given CPU", },
#endif
STEXI
@item nmi @var{cpu}
Inject an NMI on the given CPU (x86 only).
ETEXI

    { "migrate", "detach:-d,uri:s", do_migrate,
      "[-d] uri", "migrate to URI (using -d to not wait for completion)" },
STEXI
@item migrate [-d] @var{uri}
Migrate to @var{uri} (using -d to not wait for completion).
ETEXI

    { "migrate_cancel", "", do_migrate_cancel,
      "", "cancel the current VM migration" },
STEXI
@item migrate_cancel
Cancel the current VM migration.
ETEXI

    { "migrate_set_speed", "value:s", do_migrate_set_speed,
      "value", "set maximum speed (in bytes) for migrations" },
STEXI
@item migrate_set_speed @var{value}
Set maximum speed to @var{value} (in bytes) for migrations.
ETEXI

    { "migrate_set_downtime", "value:s", do_migrate_set_downtime,
      "value", "set maximum tolerated downtime (in seconds) for migrations" },

STEXI
@item migrate_set_downtime @var{second}
Set maximum tolerated downtime (in seconds) for migration.
ETEXI

#if defined(TARGET_I386)
    { "drive_add", "pci_addr:s,opts:s", drive_hot_add,
                                        "[[<domain>:]<bus>:]<slot>\n"
                                        "[file=file][,if=type][,bus=n]\n"
                                        "[,unit=m][,media=d][index=i]\n"
                                        "[,cyls=c,heads=h,secs=s[,trans=t]]\n"
                                        "[snapshot=on|off][,cache=on|off]",
                                        "add drive to PCI storage controller" },
#endif
STEXI
@item drive_add
Add drive to PCI storage controller.
ETEXI

#if defined(TARGET_I386)
    { "pci_add", "pci_addr:s,type:s,opts:s?", pci_device_hot_add, "auto|[[<domain>:]<bus>:]<slot> nic|storage [[vlan=n][,macaddr=addr][,model=type]] [file=file][,if=type][,bus=nr]...", "hot-add PCI device" },
#endif
STEXI
@item pci_add
Hot-add PCI device.
ETEXI

#if defined(TARGET_I386)
    { "pci_del", "pci_addr:s", do_pci_device_hot_remove, "[[<domain>:]<bus>:]<slot>", "hot remove PCI device" },
#endif
STEXI
@item pci_del
Hot remove PCI device.
ETEXI

    { "host_net_add", "device:s,opts:s?", net_host_device_add,
      "tap|user|socket|vde|dump [options]", "add host VLAN client" },
STEXI
@item host_net_add
Add host VLAN client.
ETEXI

    { "host_net_remove", "vlan_id:i,device:s", net_host_device_remove,
      "vlan_id name", "remove host VLAN client" },
STEXI
@item host_net_remove
Remove host VLAN client.
ETEXI

#ifdef CONFIG_SLIRP
    { "hostfwd_add", "arg1:s,arg2:s?,arg3:s?", net_slirp_hostfwd_add,
      "[vlan_id name] [tcp|udp]:[hostaddr]:hostport-[guestaddr]:guestport",
      "redirect TCP or UDP connections from host to guest (requires -net user)" },
    { "hostfwd_remove", "arg1:s,arg2:s?,arg3:s?", net_slirp_hostfwd_remove,
      "[vlan_id name] [tcp|udp]:[hostaddr]:hostport",
      "remove host-to-guest TCP or UDP redirection" },
#endif
STEXI
@item host_net_redir
Redirect TCP or UDP connections from host to guest (requires -net user).
ETEXI

    { "balloon", "value:i", do_balloon,
      "target", "request VM to change it's memory allocation (in MB)" },
STEXI
@item balloon @var{value}
Request VM to change its memory allocation to @var{value} (in MB).
ETEXI

    { "set_link", "name:s,up_or_down:s", do_set_link,
      "name up|down", "change the link status of a network adapter" },
STEXI
@item set_link @var{name} [up|down]
Set link @var{name} up or down.
ETEXI

    { "watchdog_action", "action:s", do_watchdog_action,
      "[reset|shutdown|poweroff|pause|debug|none]", "change watchdog action" },
STEXI
@item watchdog_action
Change watchdog action.
ETEXI

    { "acl_show", "aclname:s", do_acl_show, "aclname",
      "list rules in the access control list" },
STEXI
@item acl_show @var{aclname}
List all the matching rules in the access control list, and the default
policy. There are currently two named access control lists,
@var{vnc.x509dname} and @var{vnc.username} matching on the x509 client
certificate distinguished name, and SASL username respectively.
ETEXI

    { "acl_policy", "aclname:s,policy:s", do_acl_policy, "aclname allow|deny",
      "set default access control list policy" },
STEXI
@item acl_policy @var{aclname} @code{allow|deny}
Set the default access control list policy, used in the event that
none of the explicit rules match. The default policy at startup is
always @code{deny}.
ETEXI

    { "acl_add", "aclname:s,match:s,policy:s,index:i?", do_acl_add, "aclname match allow|deny [index]",
      "add a match rule to the access control list" },
STEXI
@item acl_allow @var{aclname} @var{match} @code{allow|deny} [@var{index}]
Add a match rule to the access control list, allowing or denying access.
The match will normally be an exact username or x509 distinguished name,
but can optionally include wildcard globs. eg @code{*@@EXAMPLE.COM} to
allow all users in the @code{EXAMPLE.COM} kerberos realm. The match will
normally be appended to the end of the ACL, but can be inserted
earlier in the list if the optional @var{index} parameter is supplied.
ETEXI

    { "acl_remove", "aclname:s,match:s", do_acl_remove, "aclname match",
      "remove a match rule from the access control list" },
STEXI
@item acl_remove @var{aclname} @var{match}
Remove the specified match rule from the access control list.
ETEXI

    { "acl_reset", "aclname:s", do_acl_reset, "aclname",
      "reset the access control list" },
STEXI
@item acl_remove @var{aclname} @var{match}
Remove all matches from the access control list, and set the default
policy back to @code{deny}.
ETEXI

#if defined(TARGET_I386)
    { "mce", "cpu_index:i,bank:i,status:l,mcg_status:l,addr:l,misc:l", do_inject_mce, "cpu bank status mcgstatus addr misc", "inject a MCE on the given CPU"},
#endif
STEXI
@item mce @var{cpu} @var{bank} @var{status} @var{mcgstatus} @var{addr} @var{misc}
Inject an MCE on the given CPU (x86 only).
ETEXI

    { "getfd", "fdname:s", do_getfd, "getfd name",
      "receive a file descriptor via SCM rights and assign it a name" },
STEXI
@item getfd @var{fdname}
If a file descriptor is passed alongside this command using the SCM_RIGHTS
mechanism on unix sockets, it is stored using the name @var{fdname} for
later use by other monitor commands.
ETEXI

    { "closefd", "fdname:s", do_closefd, "closefd name",
      "close a file descriptor previously passed via SCM rights" },
STEXI
@item closefd @var{fdname}
Close the file descriptor previously assigned to @var{fdname} using the
@code{getfd} command. This is only needed if the file descriptor was never
used by another monitor command.
ETEXI

STEXI
@end table
ETEXI
