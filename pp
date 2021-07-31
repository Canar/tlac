#!/usr/bin/env ruby
require 'json'
require 'pp'
pp JSON.parse(File.read(ARGV[0]))
