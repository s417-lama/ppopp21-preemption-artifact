linewidth = 2

intvl = 1
# intvl = 2
threads = [55, 110, 220, 440, 880, 1760, 3520, 7040, 14080]
size = 8

result_dir = "~/lammps/results/latest"

data = [
  {"sync"      , "Sync"              , "#1f77b4", "dot"    , "square-open" },
  {"async"     , "Async"             , "#ff7f0e", "dashdot", "diamond-open"},
  {"preemption", "Async + Preemption", "#2ca02c", "solid"  , "circle"      },
]

get_statistics_of_files = fn path ->
  data =
    path
    |> Path.expand()
    |> Path.wildcard()
    |> Enum.map(fn path ->
      File.stream!(path)
      |> Stream.map(&String.split/1)
      |> Stream.map(fn
        ["[rank", _, "worker", _, "busy", "ratio:", ratio] -> Float.parse(ratio) |> elem(0)
        _                                                  -> nil
      end)
      |> Stream.filter(&!is_nil(&1))
      |> Stream.map(fn x -> 1 - x end)
      |> Enum.to_list()
      |> Statistics.mean()
    end)
  {Statistics.median(data), Statistics.stdev(data)}
end

data
|> Enum.map(fn {prefix, title, color, dash, symbol} ->
  {ys, errors} =
    Enum.map(threads, fn thread ->
      get_statistics_of_files.("#{result_dir}/abt_#{prefix}_intvl_#{intvl}_thread_#{thread}_size_#{size}_*.out")
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
  height: 400,
  separators: ".",
  xaxis: %{
    type: "log",
    title: %{text: "# of analysis threads"},
    showline: true,
    tickvals: threads,
    exponentformat: "none",
  },
  yaxis: %{
    title: %{text: "Worker Idleness"},
    showline: true,
    range: [0, 0.48],
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
    l: 70,
    r: 0,
    b: 100,
    t: 10,
    pad: 0,
  },
})
|> PlotlyEx.show()
