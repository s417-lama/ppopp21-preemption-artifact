linewidth = 2

intvl = 1
# intvl = 2
threads = [55, 110, 220, 440, 880, 1760, 3520, 7040, 14080]
size = 8

result_dir = "~/lammps/results/2020-04-02_09-41-29"
# result_dir = "~/lammps/results/latest"

data = [
  # {"sync"      , "Sync"              , "#1f77b4", "dot"    , "square-open" },
  {"async"     , "Nonpreemptive Argobots", "#ff7f0e", "dashdot", "diamond-open"},
  {"preemption", "Preemptive Argobots"   , "#2ca02c", "solid"  , "circle"      },
]

get_statistics_of_files = fn(path, op_fn) ->
  data =
    path
    |> Path.expand()
    |> Path.wildcard()
    |> Enum.map(fn path ->
      File.stream!(path)
      |> Stream.map(&String.split/1)
      |> Stream.map(fn
        ["Loop", "time", "of", time | _] -> Float.parse(time)
        _                                -> nil
      end)
      |> Stream.filter(&!is_nil(&1))
      |> Enum.to_list()
      |> case do
        [{time, ""}] -> time
        _            -> nil
      end
    end)
    |> Enum.map(op_fn)
  {Statistics.median(data), Statistics.stdev(data)}
end

{baseline, _} = get_statistics_of_files.("#{result_dir}/no_analysis_*.out", &(&1))
# baseline = get_median_of_files.("#{result_dir}/abt_no_analysis_*.out")

data
|> Enum.map(fn {prefix, title, color, dash, symbol} ->
  {ys, errors} =
    Enum.map(threads, fn thread ->
      get_statistics_of_files.("#{result_dir}/#{prefix}_intvl_#{intvl}_thread_#{thread}_*.out", fn x -> x / baseline - 1 end)
      # m = get_median_of_files.("#{result_dir}/abt_#{prefix}_intvl_#{intvl}_thread_#{thread}_size_#{size}_*.out")
      # m / baseline - 1
    end)
    |> Enum.unzip()
  %{
    x: threads,
    y: ys,
    error_y: %{
      type: "data",
      array: errors,
      visible: true,
    },
    type: "scatter",
    name: title,
    line: %{
      color: color,
      width: linewidth,
      dash: dash,
    },
    marker: %{
      symbol: symbol,
      size:   12,
      line:   %{width: linewidth, color: color},
    },
  }
end)
|> PlotlyEx.plot(%{
  width:  400,
  height: 360,
  separators: ".",
  xaxis: %{
    type: "log",
    title: %{text: "# of analysis threads"},
    showline: true,
    tickvals: threads,
    exponentformat: "none",
  },
  yaxis: %{
    title: %{text: "Overhead"},
    tickformat: ",.0%",
    showline: true,
    range: [0, 0.76],
    # range: [0, 0.38],
    dtick: 0.1,
  },
  font: %{
    size: 22,
  },
  legend: %{
      x: 0.4,
      y: 1.0,
  },
  margin: %{
    l: 80,
    r: 0,
    b: 100,
    t: 10,
    pad: 0,
  },
})
|> PlotlyEx.show()
