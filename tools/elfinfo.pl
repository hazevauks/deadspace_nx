#!/usr/bin/perl
# elfinfo.pl: what a 32-bit ARM .so needs, exports and imports.
# Stands in for readelf on a machine without binutils.
#   perl elfinfo.pl <lib.so> [needed|exports|imports|jni|all]
use strict;
use warnings;

my ($path, $mode) = @ARGV;
$mode ||= 'all';
die "usage: elfinfo.pl <lib.so> [needed|exports|imports|jni|all]\n" unless $path;

open(my $fh, '<:raw', $path) or die "$path: $!\n";
my $data = do { local $/; <$fh> };
close($fh);

my ($magic, $class, $endian) = unpack('a4 C C', $data);
die "not an ELF\n" unless $magic eq "\x7fELF";
die "not a 32-bit little-endian ELF\n" unless $class == 1 && $endian == 1;

my ($type, $machine, $ver, $entry, $phoff, $shoff, $flags, $ehsize,
    $phentsize, $phnum, $shentsize, $shnum, $shstrndx) =
  unpack('v v V V V V V v v v v v v', substr($data, 16));

# Program headers: the load segments map addresses to file offsets.
my (@loads, $dyn_off, $dyn_size);
for my $i (0 .. $phnum - 1) {
  my ($p_type, $p_offset, $p_vaddr, $p_paddr, $p_filesz, $p_memsz, $p_flags, $p_align) =
    unpack('V8', substr($data, $phoff + $i * $phentsize, 32));
  push @loads, [$p_vaddr, $p_offset, $p_filesz, $p_memsz, $p_flags] if $p_type == 1;
  ($dyn_off, $dyn_size) = ($p_offset, $p_filesz) if $p_type == 2;
}

sub off {
  my ($va) = @_;
  for my $l (@loads) {
    return $va - $l->[0] + $l->[1] if $va >= $l->[0] && $va < $l->[0] + $l->[2];
  }
  return undef;
}

my (%dyn, @needed_off);
for (my $o = $dyn_off; $o < $dyn_off + $dyn_size; $o += 8) {
  my ($tag, $val) = unpack('V V', substr($data, $o, 8));
  last if $tag == 0;
  if ($tag == 1) { push @needed_off, $val; } else { $dyn{$tag} = $val; }
}

my $strtab = off($dyn{5});
my $symtab = off($dyn{6});
sub str { my ($o) = @_; return unpack('Z*', substr($data, $strtab + $o, 512)); }

# The symbol count comes from the hash table (DT_HASH), or from the section
# headers when the library only has a GNU hash.
my $nsyms = 0;
if (defined $dyn{4}) {
  my (undef, $nchain) = unpack('V V', substr($data, off($dyn{4}), 8));
  $nsyms = $nchain;
} else {
  for my $i (0 .. $shnum - 1) {
    my ($n, $t, $f, $a, $o, $s, $l, $in, $al, $es) =
      unpack('V10', substr($data, $shoff + $i * $shentsize, 40));
    $nsyms = $s / 16 if $t == 11;
  }
}

my (@exports, @imports);
for my $i (1 .. $nsyms - 1) {
  my ($name, $value, $size, $info, $other, $shndx) =
    unpack('V V V C C v', substr($data, $symtab + $i * 16, 16));
  my $n = str($name);
  next if $n eq '';
  my $bind = $info >> 4;
  my $typ = $info & 0xf;
  if ($shndx == 0) { push @imports, [$n, $bind, $typ]; }
  else { push @exports, [$n, $value, $size, $typ]; }
}

if ($mode eq 'all' || $mode eq 'needed') {
  printf "machine=%d type=%d flags=0x%x entry=0x%x\n", $machine, $type, $flags, $entry;
  for my $l (@loads) {
    printf "LOAD vaddr=0x%08x off=0x%08x filesz=0x%x memsz=0x%x flags=%d\n", @$l;
  }
  print "SONAME ", str($dyn{14}), "\n" if defined $dyn{14};
  print "NEEDED ", str($_), "\n" for @needed_off;
  printf "INIT_ARRAY 0x%x (%d entries)\n", $dyn{25}, ($dyn{27} || 0) / 4 if defined $dyn{25};
  printf "symbols=%d exports=%d imports=%d\n", $nsyms, scalar(@exports), scalar(@imports);
}
if ($mode eq 'all' || $mode eq 'jni') {
  print "JNI $_->[0]\n" for sort { $a->[0] cmp $b->[0] } grep { $_->[0] =~ /^(Java_|JNI_On|ANativeActivity_)/ } @exports;
}
if ($mode eq 'exports') {
  printf "%08x %6d %s\n", $_->[1], $_->[2], $_->[0] for sort { $a->[0] cmp $b->[0] } @exports;
}
if ($mode eq 'all' || $mode eq 'imports') {
  print "IMPORT $_->[0]", ($_->[1] == 2 ? " (weak)" : ""), "\n" for sort { $a->[0] cmp $b->[0] } @imports;
}
