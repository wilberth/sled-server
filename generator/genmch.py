#
# Generates state machine source
#  Usage: python genmch.py SOURCE.XML TARGET.C TARGET.H
#

import sys
import xml.etree.ElementTree as ElementTree

if len(sys.argv) < 4:
  print "Invalid argument count. Usage: python genmch.py SOURCE.XML TARGET.C TARGET.H"

source = sys.argv[1]
cfile = open(sys.argv[2], 'w')
hfile = open(sys.argv[3], 'w')

if not cfile or not hfile:
  print "Could not open output files"

try:
  root = ElementTree.parse(source).getroot()
except Exception as e:
  print "Could not parse input XML: " + str(e.message)
  sys.exit(1)

prefix = root.attrib['prefix']


####################
# HEADER GENERATOR #
####################

hfile.write("// WARNING: Automatically generated file! Do not modify!\n")
hfile.write("#ifndef __{}_H__\n".format(prefix.upper()))
hfile.write("#define __{}_H__\n\n".format(prefix.upper()))

hfile.write("struct {}_t;\n\n".format(prefix))

# Write list of events to header
hfile.write("enum {}_event_t {{\n".format(prefix))
for element in root.findall("./events/event"):
  hfile.write("\t{},\n".format(element.text))
hfile.write("};\n\n")

# Write list of states to header
hfile.write("enum {}_state_t {{\n".format(prefix))
for element in root.findall("./states/state"):
  hfile.write("\t{},\n".format(element.text))
hfile.write("};\n\n")

for element in root.findall("./include"):
  hfile.write(element.text + "\n")

# Functions
fields = list()
for element in root.findall("./fields/field"):
  fields.append(element.attrib['type'] + element.text)
args = ", ".join(fields)

hfile.write("{0}_t *{0}_create({1});\n".format(prefix, args))
hfile.write("void {0}_destroy({0}_t **machine);\n".format(prefix))
hfile.write("{0}_state_t {0}_active_state({0}_t *machine);\n".format(prefix))
hfile.write("void {0}_handle_event({0}_t *machine, {0}_event_t event);\n".format(prefix))
hfile.write("void {0}_set_callback_payload({0}_t *machine, void *payload);\n".format(prefix))
hfile.write("\n")

for element in root.findall("./callbacks/callback"):
  hfile.write("typedef void(*{0}_{1}_handler_t)({0}_t *machine, void *payload);\n".format(prefix, element.text))
  hfile.write("void {0}_set_{1}_handler({0}_t *machine, {0}_{1}_handler_t handler);\n".format(prefix, element.text))
  hfile.write("\n")

hfile.write("#endif\n\n")


####################
# SOURCE GENERATOR #
####################

cfile.write("// WARNING: Automatically generated file! Do not modify!\n")
cfile.write("#include \"{0}.h\"\n".format(prefix))
cfile.write("#include <assert.h>\n")
cfile.write("#include <stdlib.h>\n")
cfile.write("#include <syslog.h>\n\n")

# Structure
cfile.write("struct {0}_t {{\n".format(prefix))
cfile.write("\t{0}_state_t state;\n".format(prefix))
cfile.write("\tvoid *payload;\n\n".format(prefix))
for element in root.findall("./callbacks/callback"):
  cfile.write("\t{0}_{1}_handler_t {1}_handler;\n".format(prefix, element.text))
cfile.write("\t\n")
for element in root.findall("./fields/field"):
  cfile.write("\t{0} {1};\n".format(element.attrib['type'], element.text))
for element in root.findall("./fields/field_decl"):
  cfile.write("\t{0} {1};\n".format(element.attrib['type'], element.text))
cfile.write("};\n\n")


cfile.write("static {0}_state_t {0}_next_state_given_event({0}_t *machine, {0}_event_t event);\n".format(prefix))
cfile.write("static void {0}_on_enter({0}_t *machine);\n".format(prefix))
cfile.write("static void {0}_on_exit({0}_t *machine);\n\n".format(prefix))


# Create function
cfile.write("{0}_t *{0}_create({1})\n{{\n".format(prefix, args))
cfile.write("\t{0}_t *machine = new {0}_t();\n\n".format(prefix))
cfile.write("\tmachine->state = {0};\n".format(root.findall("initial")[0].text))
cfile.write("\tmachine->payload = NULL;\n\n")
for element in root.findall("./fields/field"):
  cfile.write("\tmachine->{0} = {0};\n".format(element.text))
cfile.write("\n")
for element in root.findall("./fields/field_decl"):
  if 'init' in element.attrib:
    cfile.write("\tmachine->{0} = {1};\n".format(element.text, element.attrib['init']))
cfile.write("\n")
for element in root.findall("./callbacks/callback"):
  cfile.write("\tmachine->{0}_handler = NULL;\n".format(element.text))
cfile.write("}\n\n")


# Destroy function
cfile.write("void {0}_destroy({0}_t **machine)\n{{\n".format(prefix))
cfile.write("\tfree(*machine);\n")
cfile.write("\t*machine = NULL;\n")
cfile.write("}\n\n")


# Active state
cfile.write("{0}_state_t {0}_active_state({0}_t *machine)\n{{\n".format(prefix))
cfile.write("\tassert(machine);\n")
cfile.write("\treturn machine->state;\n")
cfile.write("}\n\n")


# Callback payload
cfile.write("void {0}_set_callback_payload({0}_t *machine, void *payload)\n{{\n".format(prefix))
cfile.write("\tmachine->payload = payload;\n")
cfile.write("}\n\n")

for element in root.findall("./callbacks/callback"):
  cfile.write("void {0}_set_{1}_handler({0}_t *machine, {0}_{1}_handler_t handler)\n{{\n".format(prefix, element.text))
  cfile.write("\tmachine->{0}_handler = handler;\n".format(element.text))
  cfile.write("}\n\n")


# State name
cfile.write("const char *{0}_statename({0}_state_t state)\n{{\n".format(prefix))
cfile.write("\tswitch(state) {\n")
for element in root.findall("./states/state"):
  cfile.write("\t\tcase {0}: return \"{0}\";\n".format(element.text))
cfile.write("\t}\n")
cfile.write("\treturn \"Invalid state\";\n")
cfile.write("}\n\n")


# Event name
cfile.write("const char *{0}_eventname({0}_event_t event)\n{{\n".format(prefix))
cfile.write("\tswitch(event) {\n")
for element in root.findall("./events/event"):
  cfile.write("\t\tcase {0}: return \"{0}\";\n".format(element.text))
cfile.write("\t}\n")
cfile.write("\treturn \"Invalid event\";\n")
cfile.write("}\n\n")


# Handle event
cfile.write("void {0}_handle_event({0}_t *machine, {0}_event_t event)\n{{\n".format(prefix))
cfile.write("\tassert(machine);\n")
cfile.write("\t{0}_state_t next_state = {0}_next_state_given_event(machine, event);\n".format(prefix))
cfile.write("\tif(!(machine->state == next_state)) {\n")
cfile.write("\t\t{0}_on_exit(machine);\n".format(prefix))
cfile.write("\t\tmachine->state = next_state;\n")
cfile.write("\t\tsyslog(LOG_DEBUG, \"%s() state changed to %s\",  __FUNCTION__, {0}_statename(machine->state));\n".format(prefix))
cfile.write("\t\t{0}_on_enter(machine);\n".format(prefix))
cfile.write("\t}\n")
cfile.write("}\n\n")


