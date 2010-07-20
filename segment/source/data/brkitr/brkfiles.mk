# *   Copyright (C) 1998-2010, International Business Machines
# *   Corporation and others.  All Rights Reserved.
BRK_RES_CLDR_VERSION = 1.8.1
# A list of txt's to build
# Note:
#
#   If you are thinking of modifying this file, READ THIS.
#
# Instead of changing this file [unless you want to check it back in],
# you should consider creating a 'brklocal.mk' file in this same directory.
# Then, you can have your local changes remain even if you upgrade or
# reconfigure ICU.
#
# Example 'brklocal.mk' files:
#
#  * To add an additional locale to the list:
#    _____________________________________________________
#    |  BRK_RES_SOURCE_LOCAL =   myLocale.txt ...
#
#  * To REPLACE the default list and only build with a few
#    locales:
#    _____________________________________________________
#    |  BRK_RES_SOURCE = ar.txt ar_AE.txt en.txt de.txt zh.txt
#
#
# Generated by LDML2ICUConverter, from LDML source files.

# Aliases without a corresponding xx.xml file (see icu-config.xml & build.xml)
BRK_RES_SYNTHETIC_ALIAS =


# All aliases (to not be included under 'installed'), but not including root.
BRK_RES_ALIAS_SOURCE = $(BRK_RES_SYNTHETIC_ALIAS)


# List of compact trie dictionary files (ctd).
BRK_CTD_SOURCE = thaidict.txt cjdict.txt


# List of break iterator files (brk).
BRK_SOURCE = word_POSIX.txt sent_el.txt char_th.txt char.txt word.txt line.txt sent.txt title.txt


# Ordinary resources
BRK_RES_SOURCE = el.txt en.txt en_US.txt en_US_POSIX.txt th.txt
