# URL helpers (mruby has no Regexp, so no CGI/URI either).
module URL
  # Percent-encodes everything but unreserved characters, e.g. for G-code in a
  # query string or Klipper object names with spaces ("temperature_sensor x").
  def self.encode(text)
    text.to_s.bytes.map do |b|
      c = b.chr
      if ("a".."z").include?(c) || ("A".."Z").include?(c) || ("0".."9").include?(c) || "-_.~".include?(c)
        c
      else
        format("%%%02X", b)
      end
    end.join
  end
end
