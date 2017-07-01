#!/usr/bin/perl
use strict;

use constant jitter => 3.0;

my $s = rand();

srand (0);
my @packets;
for (my $i = 0; $i < 10; $i++)
  {
    my @packet = ();
    # build the packet: 10 transitions? Sure, why not.
    for (0..16)
      {
        push @packet, int(jitter*2 + 4*int(1+rand()*5));
      }
    push @packets, \@packet;
  }

srand($s);

for (my $i = 0; $i < 10; $i++)
{
  my @packet = @{ $packets[$i] };
  my @times;
  my $t = 0.0;
  for (@packet)
    {
      $t += $_;
      push @times, $t;
    }

  open(OUT, ">key_$i");
  open(OUTB, ">key_$i.bin");
  # Print the packet out 10 times, with some jitter;
  for (0..100)
    {
      my @jittered_times = map
        {
          my $d = (2.0*rand()*jitter)- jitter;
          $d + $_;
        }
          @times;
      my $t = 0;
      print OUT "{ ";
      for (@jittered_times)
        {
          my $n = int ($_ - $t);
          print OUT $n." ";
          $t = $_;
          print OUTB  (sprintf "%c%c", ($n>>8)&255,
                       ($n&255));
        }
      print OUT " }\n";
      print OUTB (sprintf "%c%c", 255, 255);
    }
  close(OUT);
  close(OUTB);
}

