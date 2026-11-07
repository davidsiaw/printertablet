# Timers, run between frames on the main thread. C++ doesn't call into Ruby
# every frame: after any change we tell it when the earliest timer is due
# (Native::UI.wake_at), and it calls Native::UI.frame (lib/ui.rb) only then.
class Timer
  @@timers = []

  attr_reader :due

  def initialize(interval, repeat, block)
    @interval = interval
    @repeat = repeat
    @block = block
    @due = Timer.now + interval
    @@timers << self
    Timer.send(:reschedule)   # private class method: framework-internal
  end

  def cancel
    @@timers.delete(self)
    Timer.send(:reschedule)
  end

  # Seconds, on the same clock C++ uses.
  def self.now
    Native::UI.now
  end

  def self.reschedule
    Native::UI.wake_at(@@timers.empty? ? -1 : @@timers.map(&:due).min)
  end

  # Runs the timers that are due (see Native::UI.frame in lib/ui.rb).
  def self.run_due(now)
    @@timers.select { |t| t.due <= now }.each do |t|
      t.send(:fire, now) if @@timers.include?(t)   # not cancelled by an earlier one
    end
  ensure
    reschedule
  end
  class << self
    private :reschedule, :run_due   # mruby has no private_class_method
  end

  private

  def fire(now)
    if @repeat
      @due += @interval
      @due = now + @interval if @due <= now   # fell behind: don't burst
    else
      @@timers.delete(self)
    end
    @block.call
  end
end
