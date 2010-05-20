# Script containing code specific to the softioc template

# Strip any leading '[s]ioc' off $name
if ($name =~ s/^s?ioc//) {
    $textsubs{NAME} = $name;
    $namesubs{NAME} = $name;
}
$textsubs{IOCNAME} = "sioc$name";
$namesubs{IOCNAME} = "sioc$name";

# The IOC's arch is the current host arch
$textsubs{TARGETARCH} = $arch;
print "Assuming sioc${name}'s CPU architecture is '$arch'\n";

1;
