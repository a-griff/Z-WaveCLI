#!/usr/bin/perl
#
# PROJECT:     Z-Wave CLI
# PROGRAM:     zwavecli
# VERSION:     0.3
#
# DESCRIPTION:
#   Standalone command-line interface for controlling and
#   managing Z-Wave devices through a USB Z-Wave controller.
#
#   Uses OpenZWave through a custom C++ shared library.
#   Supports confirmed device inclusion and exclusion,
#   node enumeration, device information, binary switch
#   control and live device status queries.
#
#   Automatically configures legacy 8-bit node addressing
#   through the patched OpenZWave library.
#
#   Designed for command-line operation, shell scripting
#   and automation without requiring a background daemon.
#
# CONFIGURATION:
#   ./zwavecli.conf
#   /etc/zwavecli.conf
#
# DEPENDENCIES:
#   Perl
#   FFI::Platypus
#   OpenZWave (patched)
#   /usr/local/lib/libozw_bridge.so
#

use strict;
use warnings;

use FFI::Platypus;
use FFI::Platypus::Buffer qw(scalar_to_buffer);
use Getopt::Long qw(GetOptions);

$| = 1;

my $VERSION = '0.3';

sub fail {
    my ($code, $message) = @_;
    print STDERR "ERROR: $message\n";
    exit $code;
}

sub usage {
    print <<"HELP";
Z-Wave CLI - Version $VERSION

Usage:
    zwavecli [-d DEVICE] COMMAND
    zwavecli [-d DEVICE] NODE COMMAND

Controller commands:
    list          List paired devices
    add           Pair a new device
    remove        Exclude a device
    version       Display program version
    help          Display this help

Device commands:
    on            Turn device ON
    off           Turn device OFF
    status        Query current device status

    info          Display device information
    remove        Remove a failed device

Options:
    -d DEVICE     Override configured serial device
    -h, --help    Display this help
    -v, --version Display program version

Examples:
    zwavecli list
    zwavecli add
    zwavecli remove

    zwavecli 2 info
    zwavecli 2 remove
    zwavecli 2 on
    zwavecli 2 off
    zwavecli 2 status
    zwavecli -d /dev/ttyACM1 2 on

Configuration search order:
    ./zwavecli.conf
    /etc/zwavecli.conf

Exit codes:
    0   Success
    1   Controller or communication failure
    2   Invalid arguments or configuration
    3   Node not found
    4   Operation failed or could not be verified
    5   Inclusion or exclusion timed out

HELP
}

# ==========================================
# DEFAULT CONFIGURATION
# ==========================================

my %config = (
    DEVICE => '/dev/ttyACM0',
);

# ==========================================
# CONFIGURATION FILE
# ==========================================

foreach my $file (
    './zwavecli.conf',
    '/etc/zwavecli.conf'
) {
    next unless -e $file;

    open(my $fh, '<', $file)
        or fail(2, "Cannot read $file: $!");

    while (my $line = <$fh>) {
        chomp($line);

        $line =~ s/#.*$//;
        $line =~ s/^\s+|\s+$//g;

        next if $line eq '';

        if ($line =~ /^([A-Z_]+)\s*=\s*(.*?)$/) {
            my ($key, $value) = ($1, $2);

            $value =~ s/^"(.*)"$/$1/;
            $value =~ s/^'(.*)'$/$1/;

            $config{$key} = $value;
        }
        else {
            fail(2, "Invalid configuration in $file: $line");
        }
    }

    close($fh);
    last;
}

# ==========================================
# COMMAND LINE
# ==========================================

my $dev = $config{DEVICE};
my $help = 0;
my $version = 0;

Getopt::Long::Configure(
    'no_auto_abbrev',
    'no_ignore_case'
);

GetOptions(
    'd|device=s' => \$dev,
    'h|help'     => \$help,
    'v|version'  => \$version,
) or exit 2;

if ($version ||
    (@ARGV == 1 && $ARGV[0] eq 'version')) {
    print "Z-Wave CLI $VERSION\n";
    exit 0;
}

if ($help || !@ARGV ||
    (@ARGV == 1 && $ARGV[0] eq 'help')) {
    usage();
    exit 0;
}

my $node;
my $cmd;

if ($ARGV[0] =~ /^\d+$/) {
    $node = shift @ARGV;
}

$cmd = lc(shift(@ARGV) // '');

fail(2, "Too many arguments") if @ARGV;

if ($cmd =~ /^(on|off|status|info)$/) {
    fail(2, "Node number required")
        unless defined($node);

    fail(2, "Invalid node number (1-232)")
        if $node < 1 || $node > 232;
}
elsif ($cmd eq "remove") {
    fail(2, "Invalid node number (1-232)")
        if defined($node) &&
           ($node < 1 || $node > 232);
}
elsif ($cmd =~ /^(list|add)$/) {
    fail(2, "$cmd does not require a node number")
        if defined($node);
}
else {
    fail(2, "Unknown command: $cmd");
}

# ==========================================
# VERIFY SERIAL DEVICE
# ==========================================

fail(1, "Serial device not found: $dev")
    unless -e $dev;

fail(1, "Not a character device: $dev")
    unless -c $dev;

fail(1, "Cannot access serial device: $dev")
    unless -r $dev && -w $dev;

# ==========================================
# LOAD OPENZWAVE BRIDGE
# ==========================================

my $ffi;

eval {
    $ffi = FFI::Platypus->new(api => 1);

    $ffi->lib('/usr/local/lib/libozw_bridge.so');

    $ffi->attach(
        ozw_init => ['string'] => 'int');

    $ffi->attach(
        ozw_shutdown => ['string'] => 'void');

    $ffi->attach(
        ozw_add => [] => 'int');

    $ffi->attach(
        ozw_remove => [] => 'int');
    $ffi->attach(
        ozw_remove_failed => ["uint8"] => "int");

    $ffi->attach(
        ozw_list => ['opaque','int'] => 'int');

    $ffi->attach(
        ozw_on => ['uint8'] => 'int');

    $ffi->attach(
        ozw_off => ['uint8'] => 'int');

    $ffi->attach(
        ozw_status => ['uint8'] => 'int');

    $ffi->attach(
        ozw_node_exists => ['uint8'] => 'int');

    $ffi->attach(
        ozw_info => ['uint8','opaque','int'] => 'int');
};

if ($@) {
    fail(1, "Unable to load OpenZWave bridge: $@");
}

# ==========================================
# INITIALIZE CONTROLLER
# ==========================================

my $initialized = 0;

END {
    if ($initialized) {
        eval { ozw_shutdown($dev); };
    }
}

if ($cmd eq "add" || ($cmd eq "remove" && !defined($node))) {
    print STDERR "Activate pairing mode on the Z-Wave device now.\n";
}

$initialized = 1;

if (ozw_init($dev) != 0) {
    fail(1, "Controller initialization failed");
}

# ==========================================
# EXECUTE COMMAND
# ==========================================


sub controller_membership {
    my $buf = "\0" x 232;
    my ($ptr, $size) = scalar_to_buffer($buf);

    my $n = ozw_list($ptr, $size);

    fail(1, "Unable to enumerate network membership")
        if $n < 0 || $n > $size;

    return map { ord(substr($buf, $_, 1)) }
        0..($n - 1);
}

sub verify_membership {
    open(my $fh, "-|", $^X, $0, "-d", $dev, "list")
        or fail(1, "Unable to verify network membership");

    my $output = do { local $/; <$fh> };

    close($fh)
        or fail(1, "Network membership verification failed");

    $output //= "";
    $output =~ s/^\s+|\s+$//g;

    return () if $output eq "none";

    fail(1, "Invalid network membership response")
        unless $output =~ /^\d+(?:,\d+)*$/;

    return split(/,/, $output);
}


sub execute_command {

    if (defined($node)) {
        my $exists = ozw_node_exists($node);

        fail(3, "Node $node not found")
            if $exists == 0;

        fail(1, "Unable to determine node availability")
            if $exists < 0;
    }

    if ($cmd eq "remove" && defined($node)) {

        print STDERR "Removing failed node $node...\n";

        my $result = ozw_remove_failed($node);

        if ($result == 0) {
            print "Node $node removed\n";
        }
        elsif ($result == 1) {
            fail(1, "Controller communication failure");
        }
        elsif ($result == 2) {
            fail(2, "Cannot remove the controller");
        }
        elsif ($result == 3) {
            fail(3, "Node $node not found");
        }
        elsif ($result == 5) {
            fail(5, "Failed-node removal timed out");
        }
        else {
            fail(4, "Controller rejected removal of node $node");
        }
    }

    elsif ($cmd eq "add" || $cmd eq "remove") {

        my $adding = $cmd eq "add";
        my $operation = $adding ? "Inclusion" : "Exclusion";

        my @before = controller_membership();

        print STDERR "Preparing $operation mode...\n";

        my $result = $adding
            ? ozw_add()
            : ozw_remove();

        print STDERR "Verifying network membership...\n";

        ozw_shutdown($dev);
        $initialized = 0;

        my @after = verify_membership();

        my %before = map { $_ => 1 } @before;
        my %after = map { $_ => 1 } @after;

        my @changed = $adding
            ? grep { !$before{$_} } @after
            : grep { !$after{$_} } @before;

        if (@changed && $result == 0) {
            print "$operation successful: node "
                . join(",", @changed) . "\n";
        }
        elsif (@changed) {
            print STDERR "WARNING: Network membership changed "
                . "without confirmed controller completion.\n";

            print STDERR ($adding ? "Registered node: "
                                 : "Removed node: ")
                . join(",", @changed) . "\n";

            exit 4;
        }
        elsif ($result == 5) {
            fail(5, "$operation timed out");
        }
        elsif ($result == 1) {
            fail(1, "Controller communication failure");
        }
        elsif ($result == 0) {
            fail(4, "$operation completed but network "
                . "membership did not change");
        }
        else {
            fail(4, "$operation failed");
        }
    }

    elsif ($cmd eq 'list') {

        my $buf = "\0" x 232;
        my ($ptr, $size) = scalar_to_buffer($buf);

        my $n = ozw_list($ptr, $size);

        fail(1, "Unable to enumerate devices")
            if $n < 0 || $n > $size;

        my @nodes = sort { $a <=> $b }
            map { ord(substr($buf, $_, 1)) }
            0..($n - 1);

        print @nodes
            ? join(",", @nodes)."\n"
            : "none\n";
    }

    elsif ($cmd eq 'info') {

        my $buf = "\0" x 32768;
        my ($ptr, $size) = scalar_to_buffer($buf);

        my $result = ozw_info($node, $ptr, $size);

        fail(3, "Node $node not found")
            if $result == 3;

        fail(1, "Controller communication failure")
            if $result == 1;

        fail(4, "Unable to retrieve node information")
            if $result != 0;

        $buf =~ s/\x00.*//s;

        print $buf;
    }

    elsif ($cmd eq 'on') {

        fail(4, "Node $node failed ON verification")
            if ozw_on($node) != 0;
    }

    elsif ($cmd eq 'off') {

        fail(4, "Node $node failed OFF verification")
            if ozw_off($node) != 0;
    }

    elsif ($cmd eq 'status') {

        my $state = ozw_status($node);

        if ($state == 1) {
            print "ON\n";
        }
        elsif ($state == 0) {
            print "OFF\n";
        }
        else {
            print "UNKNOWN\n";
            fail(4, "Unable to verify node $node status");
        }
    }
}

eval {
    execute_command();
};

if ($@) {
    fail(1, "Unexpected operation failure: $@");
}

exit 0;
