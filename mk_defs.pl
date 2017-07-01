#!/usr/bin/perl

use strict;
use warnings;

my @strings;
my %strings;
for (<>) {
  for (split /\b/, $_) {
    if (m{^STR__([a-zA-Z_0-9]+)$}
        && ! exists $strings{$1}) {
      push @strings, $1;
      $strings{$1} = 1;
    }
  }
}

print "enum Str_Enum {\n";
print "  STR_Unknown,\n";
for (@strings) {
  print "  STR__$_,\n"
}
print "  STR_NumStrs\n";
print "};\n";

sub gen_block {
  my ($t, $str) = @_;
  return "{\n".(join '', map { "$t    $_\n" } split "\n", $str)."$t  }\n";
}

sub gen_switch_node
  {
    my ($map, $unknown, $prefix, $suffixes, $t, $case) = @_;
    my @suffixes = @$suffixes;
    my $i = length ($prefix);
    if ($i > 1 ) {
      print "$t  /* '$prefix' */\n";
    }
    if (int @suffixes == 1) {
      if (length $suffixes[0] == 0) {
        print "$t  if (str[$i] == '\\0') ".gen_block($t, $map->{$prefix});
        print "$t  else ".gen_block ($t, $unknown);
        return;
      } else {
        my $cmp = $case? "strcasecmp" : "strcmp";
        print "$t  if (!$cmp(str+$i, \"$suffixes[0]\"))"
          .gen_block($t, $map->{$prefix.$suffixes[0]});
        print "$t  else {\n";
        print "$t    $unknown\n";
        print "$t  }\n";

        return;
      }
    }
    # Split suffixes by common prefix.
    my %char;
    for (@suffixes) {
      my ($c, $s);
      if (length $_ == 0) {
        ($c, $s) = ('', '');
      } else {
        ($c, $s) = (substr($_, 0, 1), substr($_, 1));
      }
      if ($case) {
        $c = lc $c;
      }
      $char{$c} ||= [];
      my $l = $char{$c};
      push @{$l}, $s;
    }
    print "$t  switch (str[$i]) {\n";
    for (sort keys %char) {
      if ($_ eq '') {
        print "$t  case '\\0':"
          .gen_block($t, $map->{$prefix});
      } else {
        print "$t  case '$_':";
        if ($case) {
          if (lc $_ ne $_) {
            print " case '".(lc $_)."':";
          } elsif (uc $_ ne $_) {
            print " case '".(uc $_)."':";
          }
        }
        print "\n";
        gen_switch_node ($map, $unknown, $prefix.$_,
                         $char{$_}, "$t  ", $case);
      }
    }
    print "$t  }\n";
  }

sub gen_switch 
  {
    my ($m, $unknown, $case) = @_;
    my %map;
    for (keys %$m) {
      if ($case) {
        $map{lc $_} = $m->{$_};
      } else {
        $map{$_} = $m->{$_};
      }
    }
    my @strings = keys %map;
    gen_switch_node (\%map, $unknown, "", \@strings, "", $case);
  }
    

print "enum Str_Enum decode_string(const char *str) {\n";
# Dead simple initial implementation: just a string of elsifs and strcmps.
my %map;
foreach (@strings) {
  $map{$_} = "return STR__$_; /* new */\n";
}
gen_switch(\%map, "return STR_Unknown;", 0);
print "  return STR_Unknown;\n";
print "}\n";

print "enum Str_Enum decode_string_nocase(const char *str) {\n";
gen_switch(\%map, "return STR_Unknown;", 1);
print "  return STR_Unknown;\n";
print "}\n";

print "const char *show_string(enum Str_Enum i) {\n";
print "  switch (i) {\n";
for (@strings) {
  print "  case STR__$_: return \"$_\";\n";
}
print "  default: return \"(unknown)\";\n";
print "  }\n";
print "};\n";

exit;

my $in;
open $in, "</usr/share/dict/words";
my @words = map { chomp; $_ } split "\n", join '', <$in>;
my %m;
for (@words)
    {
      $m{$_} = "return \"$_\";";
    }
gen_switch (\%m, "return NULL;");
