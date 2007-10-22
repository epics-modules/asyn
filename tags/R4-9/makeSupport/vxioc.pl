# Script containing code specific to the vxioc template

# Strip any leading 'ioc' off $name
if ($name =~ s/^ioc//) {
    $textsubs{NAME} = $name;
    $namesubs{NAME} = $name;
}
$textsubs{IOCNAME} = "ioc$name";
$namesubs{IOCNAME} = "ioc$name";

# Determine the IOC's CPU architecture, vxWorks-something

# Arch can be given on the command line:
my $t_a = shift;

# If not ...
unless(defined $t_a) {
    # See what archs Base is built for
    opendir ARCHS, "$base/bin";
    my @archs = grep {m/vxWorks-/} readdir ARCHS;
    closedir ARCHS;
    
    # This case is easy:
    if (@archs == 1) {
	$t_a = $archs[0];
	print "Assuming ioc${name}'s CPU architecture is '$t_a'\n";
    } else {
	# Need input from the user:
	if (! @archs) {
	    print "Base has not been built for vxWorks yet.\n";
	} else {
	    print "Base has these vxWorks architectures available:\n";
	    foreach (@archs) {
		print "\t$_\n";
	    }
	}
	print "Which architecture is ioc${name}'s CPU? ";
	$t_a = <STDIN>;
	chomp $t_a;
    }
}

print "WARNING: Base is not built for '$t_a'\n"
    unless -d "$base/bin/$t_a";

$textsubs{TARGETARCH} = $t_a;

1;
